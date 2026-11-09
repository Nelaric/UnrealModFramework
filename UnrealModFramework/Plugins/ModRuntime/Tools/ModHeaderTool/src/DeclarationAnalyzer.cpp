// Copyright (c) 2026 Nelaric Contributors
#include "GenerationModel.h"

#include "ClangLibrary.h"
#include "Hash.h"
#include "SourceLexer.h"
#include "ModExportMetadataSchema.h"
#include "ModHeaderTool/MetadataOptions.h"
#include "ModHeaderTool/MetadataParser.h"
#include "ModHeaderTool/MetadataValidator.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>

namespace Mod::HeaderTool
{
namespace
{
std::string Key(const fs::path& Path)
{
	std::string Text = PathToUtf8(fs::absolute(Path).lexically_normal());
#ifdef _WIN32
	std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char C) { return std::tolower(C); });
#endif
	return Text;
}

struct FLocation
{
	fs::path File;
	unsigned Offset = 0;
};

FLocation Location(const FClangLibrary& Clang, CXSourceLocation Source)
{
	CXFile File = nullptr;
	FLocation Result;
	Clang.clang_getExpansionLocation(Source, &File, nullptr, nullptr, &Result.Offset);
	if (File)
	{
		Result.File = PathFromUtf8(Clang.String(Clang.clang_getFileName(File)));
	}
	return Result;
}

std::string QualifiedName(const FClangLibrary& Clang, CXCursor Cursor)
{
	std::string Result = Clang.String(Clang.clang_getCursorSpelling(Cursor));
	for (CXCursor Parent = Clang.clang_getCursorSemanticParent(Cursor);
	     Parent.kind != CXCursor_TranslationUnit && Parent.kind != CXCursor_InvalidFile;
	     Parent = Clang.clang_getCursorSemanticParent(Parent))
	{
		if (Parent.kind != CXCursor_Namespace)
		{
			throw std::runtime_error(
			    "Nested classes and class templates are not supported by the UObject API backend.");
		}
		const auto Name = Clang.String(Clang.clang_getCursorSpelling(Parent));
		if (Name.empty())
		{
			throw std::runtime_error("Anonymous namespaces cannot own exported types.");
		}
		Result = Name + "::" + Result;
	}
	return Result;
}

[[noreturn]] void Fail(const FExportMacro& Macro, const std::string& Reason)
{
	throw std::runtime_error(PathToUtf8(Macro.File) + "(" + std::to_string(Macro.Line) + "," +
	                         std::to_string(Macro.Column) + "): error: " + Macro.Name + ": " + Reason);
}

std::string Field(const FExportMacro& Macro, std::string_view Name)
{
	for (const auto& Value : Macro.Fields)
	{
		if (Value.Name == Name)
		{
			return Value.Value.Text;
		}
	}
	return {};
}

std::vector<std::string> SplitArguments(std::string_view Text)
{
	std::vector<std::string> Result;
	std::string Current;
	char Quote = 0;
	bool bToken = false;
	for (std::size_t I = 0; I < Text.size(); ++I)
	{
		const char C = Text[I];
		if (C == '\\' && I + 1 < Text.size() && Text[I + 1] == '"')
		{
			Current.push_back('"');
			++I;
			bToken = true;
		}
		else if (C == '"' || C == '\'')
		{
			if (Quote == C)
			{
				Quote = 0;
			}
			else if (!Quote)
			{
				Quote = C;
			}
			else
			{
				Current.push_back(C);
			}
			bToken = true;
		}
		else if (!Quote && std::isspace(static_cast<unsigned char>(C)))
		{
			if (bToken)
			{
				Result.push_back(std::move(Current));
				Current.clear();
				bToken = false;
			}
		}
		else
		{
			Current.push_back(C);
			bToken = true;
		}
	}
	if (Quote)
	{
		throw std::runtime_error("Unclosed quote in compiler arguments.");
	}
	if (bToken)
	{
		Result.push_back(std::move(Current));
	}
	return Result;
}

void ExpandResponses(const std::vector<std::string>& Arguments, const fs::path& Directory,
                     std::vector<std::string>& Expanded, std::set<fs::path>& Stack, FGenerationModel& Model)
{
	for (const auto& Argument : Arguments)
	{
		if (!Argument.starts_with('@'))
		{
			Expanded.push_back(Argument);
			continue;
		}
		const fs::path Response = fs::canonical(Directory / PathFromUtf8(Argument.substr(1)));
		if (!Stack.insert(Response).second)
		{
			throw std::runtime_error("Recursive compiler response file.");
		}
		Model.Dependencies.insert(Response);
		ExpandResponses(SplitArguments(ReadTextFile(Response)), Directory, Expanded, Stack, Model);
		Stack.erase(Response);
	}
}

std::vector<std::string> AnalysisCommand(const std::vector<std::string>& Command, const fs::path& Directory,
                                         const fs::path& Unit, FGenerationModel& Model)
{
	std::vector<std::string> Expanded;
	std::set<fs::path> Stack;
	ExpandResponses(Command, Directory, Expanded, Stack, Model);
	if (Expanded.empty())
	{
		throw std::runtime_error("Empty compilation command.");
	}
	for (const auto& Argument : Expanded)
	{
		if (Argument.starts_with("/Yu") || Argument == "-include-pch")
		{
			throw std::runtime_error(
			    "Analysis requires fresh NoPCH compiler inputs. Run UBT -Mode=GenerateClangDatabase -NoPCH immediately "
			    "before MHT; a normal build may replace its response files.");
		}
	}
	std::string Driver = PathToUtf8(PathFromUtf8(Expanded.front()).filename());
	std::transform(Driver.begin(), Driver.end(), Driver.begin(), [](unsigned char C) { return std::tolower(C); });
	const bool bMsvc = Driver == "cl.exe" || Driver == "clang-cl.exe";
	std::vector<std::string> Result{"clang", "-x", "c++", "-working-directory", PathToUtf8(Directory)};
	if (bMsvc)
	{
		Result.insert(Result.end(), {"-fms-extensions", "-fms-compatibility", "-fdelayed-template-parsing"});
		for (std::size_t I = 1; I < Expanded.size(); ++I)
		{
			const std::string& Arg = Expanded[I];
			bool bConsumed = false;
			for (const auto& [Prefix, Replacement] : std::vector<std::pair<std::string, std::string>>{
			         {"/external:I", "-isystem"}, {"/FI", "-include"}, {"/I", "-I"}, {"/D", "-D"}, {"/U", "-U"}})
			{
				if (!Arg.starts_with(Prefix))
				{
					continue;
				}
				const std::string Value = Arg.size() == Prefix.size() ? Expanded.at(++I) : Arg.substr(Prefix.size());
				Result.push_back(Replacement);
				Result.push_back(Value);
				bConsumed = true;
				break;
			}
			if (bConsumed)
			{
				continue;
			}
			if (Arg.starts_with("/std:"))
			{
				Result.push_back("-std=" + Arg.substr(5));
			}
			else if (Arg == "/J")
			{
				Result.push_back("-funsigned-char");
			}
			else if (Arg == "/GR-")
			{
				Result.push_back("-fno-rtti");
			}
			else if (Arg == "/GR")
			{
				Result.push_back("-frtti");
			}
			else if (Arg.starts_with("/EH"))
			{
				Result.push_back("-fexceptions");
			}
			else if (Arg.starts_with("/Zp"))
			{
				Result.push_back("-fpack-struct=" + Arg.substr(3));
			}
			else if (Arg == "/fp:fast")
			{
				Result.push_back("-ffast-math");
			}
		}
	}
	else
	{
		for (std::size_t I = 1; I < Expanded.size(); ++I)
		{
			const std::string& Arg = Expanded[I];
			if (Arg == "-c" || Arg == "-fsyntax-only")
			{
				continue;
			}
			if (Arg == "-o" || Arg == "-MF" || Arg == "-MT" || Arg == "-MQ" || Arg == "-include-pch")
			{
				++I;
				continue;
			}
			if (Arg == "-MD" || Arg == "-MMD" || Arg == "-MP")
			{
				continue;
			}
			if (Key(Directory / PathFromUtf8(Arg)) == Key(Unit))
			{
				continue;
			}
			Result.push_back(Arg);
		}
	}
	Result.insert(Result.end(), Model.Input.AnalysisArguments.begin(), Model.Input.AnalysisArguments.end());
	Result.push_back(PathToUtf8(Unit));
	std::string Fingerprint;
	for (const auto& Arg : Result)
	{
		Fingerprint += NormalizeInput(Model.Input, Arg) + '\n';
	}
	Model.Commands.push_back(std::move(Fingerprint));
	return Result;
}

struct FSource
{
	fs::path Path;
	std::string Text;
	std::vector<FExportMacro> Macros;
	bool bCovered = false;
	std::set<std::size_t> Active;
};

struct FTraversal
{
	FClangLibrary& Clang;
	CXTranslationUnit Tu;
	std::map<std::string, FSource>& Sources;
	std::vector<CXCursor> Methods;
	std::set<std::string> Active;
	std::map<std::string, std::string> Macros;
	std::exception_ptr Error;
};

CXChildVisitResult Visit(CXCursor Cursor, CXCursor, CXClientData Data)
{
	auto& Context = *static_cast<FTraversal*>(Data);
	try
	{
		if (Cursor.kind == CXCursor_MacroDefinition)
		{
			const auto Name = Context.Clang.String(Context.Clang.clang_getCursorSpelling(Cursor));
			if (Name.empty() || (Name.front() == '_' && Name != "_WIN64" && Name != "_M_X64") || Name.ends_with("_API"))
			{
				return CXChildVisit_Continue;
			}
			const auto File = Location(Context.Clang, Context.Clang.clang_getCursorLocation(Cursor)).File;
			const auto Leaf = PathToUtf8(File.filename());
			if (!File.empty() && PathToUtf8(File).find("<command line>") == std::string::npos &&
			    !Leaf.starts_with("Definitions.") && !Leaf.starts_with("SharedDefinitions."))
			{
				return CXChildVisit_Continue;
			}
			CXToken* Tokens = nullptr;
			unsigned Count = 0;
			Context.Clang.clang_tokenize(Context.Tu, Context.Clang.clang_getCursorExtent(Cursor), &Tokens, &Count);
			if (Count == 2)
			{
				const auto Value = Context.Clang.String(Context.Clang.clang_getTokenSpelling(Context.Tu, Tokens[1]));
				if (!Value.empty() && (std::isdigit(static_cast<unsigned char>(Value.front())) ||
				                       Value.front() == '"' || Name == "UE_TARGET_NAME" ||
				                       Name == "UBT_COMPILED_PLATFORM" || Name == "UMF_MHT_TARGET_CONFIGURATION"))
				{
					Context.Macros[Name] = Value;
				}
			}
			Context.Clang.clang_disposeTokens(Context.Tu, Tokens, Count);
			return CXChildVisit_Continue;
		}
		if (Cursor.kind == CXCursor_Namespace || Cursor.kind == CXCursor_LinkageSpec)
		{
			return CXChildVisit_Recurse;
		}
		if (Cursor.kind != CXCursor_MacroExpansion && Cursor.kind != CXCursor_CXXMethod &&
		    Cursor.kind != CXCursor_ClassDecl && Cursor.kind != CXCursor_StructDecl)
		{
			return CXChildVisit_Continue;
		}
		if (Cursor.kind == CXCursor_MacroExpansion)
		{
			const auto Name = Context.Clang.String(Context.Clang.clang_getCursorSpelling(Cursor));
			if (Name != "MOD_EXPORT_API" && Name != "MOD_EXPORT_EVENT" && Name != "MOD_EXPORT_HOOK")
			{
				return CXChildVisit_Continue;
			}
		}
		const auto LocationValue = Location(Context.Clang, Context.Clang.clang_getCursorLocation(Cursor));
		auto Source =
		    LocationValue.File.empty() ? Context.Sources.end() : Context.Sources.find(Key(LocationValue.File));
		if (Cursor.kind == CXCursor_MacroExpansion && Source != Context.Sources.end())
		{
			const auto Name = Context.Clang.String(Context.Clang.clang_getCursorSpelling(Cursor));
			if (Name == "MOD_EXPORT_API" || Name == "MOD_EXPORT_EVENT" || Name == "MOD_EXPORT_HOOK")
			{
				Source->second.Active.insert(LocationValue.Offset);
				Context.Active.insert(Source->first + ":" + std::to_string(LocationValue.Offset));
			}
		}
		if (Cursor.kind == CXCursor_CXXMethod && Source != Context.Sources.end())
		{
			Context.Methods.push_back(Cursor);
		}
		if (Cursor.kind == CXCursor_Namespace || Cursor.kind == CXCursor_LinkageSpec ||
		    ((Cursor.kind == CXCursor_ClassDecl || Cursor.kind == CXCursor_StructDecl) &&
		     Source != Context.Sources.end()))
		{
			return CXChildVisit_Recurse;
		}
		return CXChildVisit_Continue;
	}
	catch (...)
	{
		Context.Error = std::current_exception();
		return CXChildVisit_Break;
	}
}

bool IsUObject(FClangLibrary& Clang, CXCursor Record, std::set<std::string>& Visited,
               std::string_view BaseName = "UObject")
{
	const auto Definition = Clang.clang_getCursorDefinition(Record);
	if (Definition.kind == CXCursor_ClassDecl || Definition.kind == CXCursor_StructDecl)
	{
		Record = Definition;
	}
	if (QualifiedName(Clang, Record) == BaseName)
	{
		return true;
	}
	if (!Visited.insert(Clang.String(Clang.clang_getCursorUSR(Record))).second)
	{
		return false;
	}
	struct FBaseState
	{
		FClangLibrary* Clang;
		std::vector<CXCursor> Bases;
	} Bases{&Clang, {}};
	Clang.clang_visitChildren(
	    Record,
	    [](CXCursor Child, CXCursor, CXClientData Data)
	    {
		    auto& State = *static_cast<FBaseState*>(Data);
		    if (Child.kind == CXCursor_CXXBaseSpecifier)
		    {
			    State.Bases.push_back(State.Clang->clang_getTypeDeclaration(
			        State.Clang->clang_getCanonicalType(State.Clang->clang_getCursorType(Child))));
		    }
		    return CXChildVisit_Continue;
	    },
	    &Bases);
	for (const auto Base : Bases.Bases)
	{
		if (IsUObject(Clang, Base, Visited, BaseName))
		{
			return true;
		}
	}
	return false;
}

const FModuleInput* OwnerModule(const FGenerationInput& Input, const fs::path& File)
{
	const FModuleInput* Result = nullptr;
	for (const auto& Module : Input.Modules)
	{
		const auto Relative = File.lexically_relative(Module.Root);
		if (!Relative.empty() && *Relative.begin() != "..")
		{
			if (Result)
			{
				throw std::runtime_error("Overlapping source modules: " + PathToUtf8(File));
			}
			Result = &Module;
		}
	}
	return Result;
}

FResolvedType& ResolveRecord(FGenerationModel& Model, FClangLibrary& Clang, CXCursor Record, const FExportMacro& Macro)
{
	const auto Definition = Clang.clang_getCursorDefinition(Record);
	if (Definition.kind == CXCursor_ClassDecl || Definition.kind == CXCursor_StructDecl)
	{
		Record = Definition;
	}
	const std::string Name = QualifiedName(Clang, Record);
	if (auto It = Model.Types.find(Name); It != Model.Types.end())
	{
		return It->second;
	}
	std::set<std::string> Visited;
	if ((Record.kind != CXCursor_ClassDecl && Record.kind != CXCursor_StructDecl) || !IsUObject(Clang, Record, Visited))
	{
		Fail(Macro, "the UObject backend requires a concrete UObject-derived class: " + Name);
	}
	struct FReflectionEvidence
	{
		FClangLibrary* Clang;
		bool bOwnStaticClass = false;
	} Evidence{&Clang};
	Clang.clang_visitChildren(
	    Record,
	    [](CXCursor Child, CXCursor, CXClientData Data)
	    {
		    auto& Evidence = *static_cast<FReflectionEvidence*>(Data);
		    if (Child.kind == CXCursor_CXXMethod && Evidence.Clang->clang_CXXMethod_isStatic(Child) &&
		        Evidence.Clang->String(Evidence.Clang->clang_getCursorSpelling(Child)) == "StaticClass")
		    {
			    Evidence.bOwnStaticClass = true;
		    }
		    return CXChildVisit_Continue;
	    },
	    &Evidence);
	if (!Evidence.bOwnStaticClass)
	{
		Fail(Macro, "exported UObject types require their own generated StaticClass declaration: " + Name);
	}
	const auto Identity = Model.Input.Types.find(Name);
	if (Identity == Model.Input.Types.end())
	{
		Fail(Macro, "missing persistent type identity for '" + Name + "'.");
	}
	const auto File = fs::canonical(Location(Clang, Clang.clang_getCursorLocation(Record)).File);
	const auto* Module = OwnerModule(Model.Input, File);
	if (!Module || (File.extension() != ".h" && File.extension() != ".hpp"))
	{
		Fail(Macro, "exported object types must be declared in a header of a participating module: " + Name);
	}
	FResolvedType Type;
	Type.Identity = Identity->second;
	Type.Module = Module->Name;
	Type.Header = File;
	Type.Symbol = "T_" + HashBytes(Type.Identity.Id).substr(0, 16);
	Visited.clear();
	Type.bLocalPlayerSubsystem = IsUObject(Clang, Record, Visited, "ULocalPlayerSubsystem");
	return Model.Types.emplace(Name, std::move(Type)).first->second;
}

FApiType ResolveType(FGenerationModel& Model, FClangLibrary& Clang, CXType Type, const FExportMacro& Macro)
{
	Type = Clang.clang_getCanonicalType(Type);
	FApiType Result;
	Result.Cpp = Clang.String(Clang.clang_getTypeSpelling(Type));
	if (Type.kind == CXType_Void)
	{
		Result.Public = "void";
		return Result;
	}
	Result.Kind = EApiTypeKind::Scalar;
	switch (Type.kind)
	{
	case CXType_Bool:
		Result.Public = "bool";
		break;
	case CXType_Float:
		Result.Public = "float";
		break;
	case CXType_Double:
		Result.Public = "double";
		break;
	case CXType_Char_S:
	case CXType_SChar:
		Result.Public = "int8";
		break;
	case CXType_Char_U:
	case CXType_UChar:
		Result.Public = "uint8";
		break;
	case CXType_Short:
		Result.Public = "int16";
		break;
	case CXType_UShort:
		Result.Public = "uint16";
		break;
	case CXType_Int:
		Result.Public = "int32";
		break;
	case CXType_UInt:
		Result.Public = "uint32";
		break;
	case CXType_LongLong:
		Result.Public = "int64";
		break;
	case CXType_ULongLong:
		Result.Public = "uint64";
		break;
	case CXType_Pointer:
	{
		const CXType Pointee = Clang.clang_getCanonicalType(Clang.clang_getPointeeType(Type));
		auto& Record = ResolveRecord(Model, Clang, Clang.clang_getTypeDeclaration(Pointee), Macro);
		if (Record.Identity.Scope != "World")
		{
			Fail(Macro, "returned objects require a World-scoped type.");
		}
		Result.Kind = EApiTypeKind::Object;
		Result.Object = Record.Identity.CppName;
		Result.bConst = Clang.clang_isConstQualifiedType(Pointee) != 0;
		Result.Public = "FModRuntimeObject<" + std::string(Result.bConst ? "const " : "") + Result.Object + ">";
		break;
	}
	default:
		Fail(Macro, "unsupported type '" + Result.Cpp + "'; require a scalar, void, or UObject pointer return.");
	}
	return Result;
}

void CheckGap(const FExportMacro& Macro, const std::string& Source, std::size_t DeclarationBegin)
{
	Detail::FLexer Lexer(Macro.File, Source, Macro.Offset + Macro.Length, false);
	for (auto Token = Lexer.Read(); Token.Begin < DeclarationBegin; Token = Lexer.Read())
	{
		if (Token.Kind == Detail::ETokenKind::Identifier && Token.Text == "UFUNCTION")
		{
			if (Lexer.Read().Punctuation != '(')
			{
				Fail(Macro, "malformed UFUNCTION between marker and member.");
			}
			int Depth = 1;
			while (Depth)
			{
				const auto Next = Lexer.Read();
				if (Next.Kind == Detail::ETokenKind::End)
				{
					Fail(Macro, "unterminated UFUNCTION.");
				}
				if (Next.Punctuation == '(')
				{
					++Depth;
				}
				if (Next.Punctuation == ')')
				{
					--Depth;
				}
			}
			continue;
		}
		Fail(Macro, "marker must immediately precede one supported member declaration.");
	}
}

FApi Bind(FGenerationModel& Model, FClangLibrary& Clang, FExportMacro Macro, const FSource& Source, CXCursor Method)
{
	if (Macro.Name != "MOD_EXPORT_API")
	{
		Fail(Macro, "code generation currently supports API declarations only.");
	}
	if (Clang.clang_getCXXAccessSpecifier(Method) != CX_CXXPublic || Clang.clang_CXXMethod_isStatic(Method) ||
	    Clang.clang_Cursor_isVariadic(Method) || Clang.clang_CXXMethod_isDeleted(Method) ||
	    Clang.clang_Type_getCXXRefQualifier(Clang.clang_getCursorType(Method)) != CXRefQualifier_None)
	{
		Fail(Macro, "require a public, non-static, non-deleted, non-variadic member without a ref qualifier.");
	}
	const auto FunctionType = Clang.clang_getCanonicalType(Clang.clang_getCursorType(Method));
	const auto FunctionSpelling = Clang.String(Clang.clang_getTypeSpelling(FunctionType));
	const int Exception = Clang.clang_getExceptionSpecificationType(FunctionType);
	if (FunctionSpelling.find("volatile") != std::string::npos ||
	    (Exception != CXCursor_ExceptionSpecificationKind_None &&
	     Exception != CXCursor_ExceptionSpecificationKind_BasicNoexcept))
	{
		Fail(Macro, "volatile and computed exception specifications are not supported.");
	}
	const auto Start = Location(Clang, Clang.clang_getRangeStart(Clang.clang_getCursorExtent(Method)));
	CheckGap(Macro, Source.Text, Start.Offset);
	auto& Owner = ResolveRecord(Model, Clang, Clang.clang_getCursorSemanticParent(Method), Macro);
	FApi Api;
	Api.Owner = Owner.Identity.CppName;
	Api.Module = Owner.Module;
	Api.Name = Clang.String(Clang.clang_getCursorSpelling(Method));
	if (Api.Name.starts_with("operator"))
	{
		Fail(Macro, "operator exports are not supported.");
	}
	Api.Id = Field(Macro, "ExportId");
	Api.Version = Field(Macro, "ExportVersion");
	Api.Scope = Field(Macro, "Scope");
	if (Api.Scope != Owner.Identity.Scope || Field(Macro, "Thread") != "GameThread" ||
	    Field(Macro, "FailureRule") != "ReturnError")
	{
		Fail(Macro, "require the owner's declared scope, GameThread, and ReturnError for this backend.");
	}
	for (const auto& Item : Macro.Fields)
	{
		if (Item.Name == "Permissions")
		{
			for (const auto& Value : Item.Value.Items)
			{
				Api.Permissions.push_back(Value.Text);
			}
		}
	}
	Api.bConst = Clang.clang_CXXMethod_isConst(Method) != 0;
	Api.bNoexcept = Exception == CXCursor_ExceptionSpecificationKind_BasicNoexcept;
	Api.SourceSignature = Api.Owner + "::" + Api.Name + " : " + FunctionSpelling;
	Api.Symbol = "A_" + HashBytes(Api.Id).substr(0, 16);
	Api.Return = ResolveType(Model, Clang, Clang.clang_getCursorResultType(Method), Macro);
	for (int I = 0; I < Clang.clang_Cursor_getNumArguments(Method); ++I)
	{
		const auto Argument = Clang.clang_Cursor_getArgument(Method, I);
		FApiParameter Parameter;
		Parameter.Name = Clang.String(Clang.clang_getCursorSpelling(Argument));
		Parameter.Type = ResolveType(Model, Clang, Clang.clang_getCursorType(Argument), Macro);
		if (Parameter.Type.Kind != EApiTypeKind::Scalar)
		{
			Fail(Macro, "parameters currently require by-value scalars.");
		}
		Api.Parameters.push_back(std::move(Parameter));
	}
	Api.Macro = std::move(Macro);
	return Api;
}
} // namespace

FGenerationModel Analyze(FGenerationInput Input, const fs::path& OptionsFile, const fs::path& ClangPath,
                         const fs::path& AnalysisDatabase)
{
	FGenerationModel Model;
	Model.Input = std::move(Input);
	Model.Dependencies = {Model.Input.File, Model.Input.Database, Model.Input.Project.File};
	Model.Dependencies.insert(AnalysisDatabase);
	const auto TargetDirectory = Model.Input.Project.Root / "Source";
	if (fs::is_directory(TargetDirectory))
	{
		for (const auto& Entry : fs::directory_iterator(TargetDirectory))
		{
			if (Entry.is_regular_file() && PathToUtf8(Entry.path().filename()).ends_with(".Target.cs"))
			{
				Model.Dependencies.insert(Entry.path());
			}
		}
	}
	if (!Model.Input.IdentityFile.empty())
	{
		Model.Dependencies.insert(Model.Input.IdentityFile);
	}
	for (const auto& Module : Model.Input.Modules)
	{
		const auto Rules = Module.Root / (Module.Name + ".Build.cs");
		if (fs::exists(Rules))
		{
			Model.Dependencies.insert(Rules);
		}
	}
	if (Model.Input.Project.bHasConfig)
	{
		Model.Dependencies.insert(Model.Input.Project.ConfigFile);
	}
	const auto Options = LoadMetadataOptions(
	    Model.Input.Project,
	    OptionsFile.empty() ? Model.Input.Project.Root / Mod::ExportMetadata::Schema::SnapshotPath : OptionsFile);
	Model.Dependencies.insert(Options.File);
	Model.Dependencies.insert(Options.Inputs.begin(), Options.Inputs.end());
	Model.Dependencies.insert(Model.Input.Project.ConfigFile);
	Model.ConfigRoots = Options.ConfigRoots;
	std::map<std::string, FSource> Sources;
	for (const auto& File : CollectSourceFiles(Model.Input.Project))
	{
		if (!OwnerModule(Model.Input, File))
		{
			continue;
		}
		Model.Dependencies.insert(File);
		FSource Source{File, ReadTextFile(File), {}, false, {}};
		Source.Macros = ScanSource(File, Source.Text);
		if (!Source.Macros.empty())
		{
			Sources.emplace(Key(File), std::move(Source));
		}
	}
	FClangLibrary Clang(ClangPath);
	Model.ClangVersion = Clang.String(Clang.clang_getClangVersion());
	const auto Index = Clang.clang_createIndex(0, 0);
	const auto IndexCleanup = std::shared_ptr<void>(Index, [&](void* Value) { Clang.clang_disposeIndex(Value); });
	const auto Database = nlohmann::json::parse(ReadTextFile(AnalysisDatabase));
	if (!Database.is_array())
	{
		throw std::runtime_error("Expected an actual UBT compilation database array.");
	}
	std::multimap<std::string, const nlohmann::json*> Entries;
	for (const auto& Entry : Database)
	{
		const auto Directory = PathFromUtf8(Entry.at("directory").get<std::string>());
		Entries.emplace(Key(Directory / PathFromUtf8(Entry.at("file").get<std::string>())), &Entry);
	}
	std::map<std::string, std::size_t> Bound;
	for (const auto& Module : Model.Input.Modules)
	{
		for (const auto& Unit : Module.TranslationUnits)
		{
			Model.Dependencies.insert(Unit);
			const auto [First, Last] = Entries.equal_range(Key(Unit));
			if (First == Last || std::next(First) != Last)
			{
				throw std::runtime_error("Require exactly one actual Target compilation command for " +
				                         PathToUtf8(Unit));
			}
			const auto& Command = *First->second;
			const auto Arguments = Command.contains("arguments")
			                           ? Command.at("arguments").get<std::vector<std::string>>()
			                           : SplitArguments(Command.at("command").get<std::string>());
			const auto Directory = PathFromUtf8(Command.at("directory").get<std::string>());
			const auto Adapted = AnalysisCommand(Arguments, Directory, Unit, Model);
			std::vector<const char*> Argv;
			for (const auto& Arg : Adapted)
			{
				Argv.push_back(Arg.c_str());
			}
			CXTranslationUnit Tu = nullptr;
			const auto Error = Clang.clang_parseTranslationUnit2FullArgv(
			    Index, nullptr, Argv.data(), static_cast<int>(Argv.size()), nullptr, 0,
			    CXTranslationUnit_DetailedPreprocessingRecord | CXTranslationUnit_SkipFunctionBodies, &Tu);
			if (Error != CXError_Success || !Tu)
			{
				throw std::runtime_error("libclang could not parse " + PathToUtf8(Unit) +
				                         "; check the locked analysis arguments.");
			}
			const auto TuCleanup = std::shared_ptr<CXTranslationUnitImpl>(
			    Tu, [&](auto* Value) { Clang.clang_disposeTranslationUnit(Value); });
			std::string Errors;
			for (unsigned I = 0; I < Clang.clang_getNumDiagnostics(Tu); ++I)
			{
				const auto Diagnostic = Clang.clang_getDiagnostic(Tu, I);
				if (Clang.clang_getDiagnosticSeverity(Diagnostic) >= CXDiagnostic_Error)
				{
					Errors += Clang.String(Clang.clang_formatDiagnostic(
					              Diagnostic, Clang.clang_defaultDiagnosticDisplayOptions())) +
					          '\n';
				}
				Clang.clang_disposeDiagnostic(Diagnostic);
			}
			if (!Errors.empty())
			{
				throw std::runtime_error(Errors);
			}
			struct FInclusionContext
			{
				FClangLibrary* Clang;
				FGenerationModel* Model;
				fs::path Directory;
			} InclusionContext{&Clang, &Model, Directory};
			Clang.clang_getInclusions(
			    Tu,
			    [](CXFile File, CXSourceLocation*, unsigned, CXClientData Data)
			    {
				    auto& Context = *static_cast<FInclusionContext*>(Data);
				    const auto Name = Context.Clang->String(Context.Clang->clang_getFileName(File));
				    if (!Name.empty())
				    {
					    const auto Path = PathFromUtf8(Name);
					    Context.Model->Dependencies.insert(
					        fs::weakly_canonical(Path.is_absolute() ? Path : Context.Directory / Path));
				    }
			    },
			    &InclusionContext);
			for (auto& [SourceKey, Source] : Sources)
			{
				for (const auto& Dependency : Model.Dependencies)
				{
					if (Key(Dependency) == SourceKey)
					{
						Source.bCovered = true;
						break;
					}
				}
			}
			FTraversal Traversal{Clang, Tu, Sources, {}, {}, {}, {}};
			Clang.clang_visitChildren(Clang.clang_getTranslationUnitCursor(Tu), Visit, &Traversal);
			if (Traversal.Error)
			{
				std::rethrow_exception(Traversal.Error);
			}
			std::string Configuration =
			    Model.Input.Configuration == "DebugGame" ? "Development" : Model.Input.Configuration;
			std::transform(Configuration.begin(), Configuration.end(), Configuration.begin(),
			               [](unsigned char C) { return std::toupper(C); });
			const auto Variant = Traversal.Macros.find("UMF_MHT_TARGET_CONFIGURATION");
			const bool bVariantMatches = Variant != Traversal.Macros.end()
			                                 ? Variant->second == Model.Input.Configuration
			                                 : Model.Input.Configuration != "DebugGame";
			if (Traversal.Macros["UE_TARGET_NAME"] != Model.Input.Target ||
			    Traversal.Macros["UE_MODULE_NAME"] != "\"" + Module.Name + "\"" ||
			    Traversal.Macros["UBT_COMPILED_PLATFORM"] != Model.Input.Platform ||
			    Traversal.Macros["UE_BUILD_" + Configuration] != "1" || !Traversal.Macros.contains("_M_X64") ||
			    !bVariantMatches)
			{
				throw std::runtime_error(
				    "Compilation database does not match the declared UBT Target/module/configuration/platform: " +
				    PathToUtf8(Unit));
			}
			if (const auto It = Model.ContextMacros.find(Module.Name);
			    It != Model.ContextMacros.end() && It->second != Traversal.Macros)
			{
				throw std::runtime_error("Inconsistent Target macro context in module " + Module.Name);
			}
			Model.ContextMacros[Module.Name] = std::move(Traversal.Macros);

			for (auto& [SourceKey, Source] : Sources)
			{
				for (auto Macro : Source.Macros)
				{
					if (!Traversal.Active.contains(SourceKey + ":" + std::to_string(Macro.Offset)))
					{
						continue;
					}
					if (Macro.Name != "MOD_EXPORT_API")
					{
						Fail(Macro, "code generation currently supports API declarations only.");
					}
					Macro.Fields = ParseMetadata(Macro, Source.Text);
					const auto Diagnostics = ValidateMetadata(Macro, &Options, Source.Text);
					if (!Diagnostics.empty())
					{
						std::string Message;
						for (const auto& Diagnostic : Diagnostics)
						{
							Message += Diagnostic + '\n';
						}
						throw std::runtime_error(Message);
					}
					CXCursor Candidate{};
					std::size_t Nearest = Source.Text.size() + 1;
					for (const auto Method : Traversal.Methods)
					{
						const auto Start =
						    Location(Clang, Clang.clang_getRangeStart(Clang.clang_getCursorExtent(Method)));
						if (!Start.File.empty() && Key(Start.File) == SourceKey &&
						    Start.Offset >= Macro.Offset + Macro.Length && Start.Offset < Nearest)
						{
							Candidate = Method;
							Nearest = Start.Offset;
						}
					}
					if (Candidate.kind != CXCursor_CXXMethod)
					{
						// This file may be active only in another participating TU.
						continue;
					}
					FApi Api;
					try
					{
						Api = Bind(Model, Clang, Macro, Source, Candidate);
					}
					catch (const std::exception& Error)
					{
						if (std::string(Error.what()).find("): error:") != std::string::npos)
						{
							throw;
						}
						Fail(Macro, Error.what());
					}
					const auto KeyValue = SourceKey + ":" + std::to_string(Macro.Offset);
					if (const auto It = Bound.find(KeyValue); It != Bound.end())
					{
						if (Model.Apis[It->second].SourceSignature != Api.SourceSignature)
						{
							Fail(Macro, "inconsistent member signature across translation units.");
						}
					}
					else
					{
						Bound.emplace(KeyValue, Model.Apis.size());
						Model.Apis.push_back(std::move(Api));
					}
				}
			}
		}
	}
	for (const auto& [SourceKey, Source] : Sources)
	{
		for (const auto& Macro : Source.Macros)
		{
			if (!Source.bCovered)
			{
				Fail(Macro, "source is not covered by a participating translation unit.");
			}
			if (Source.Active.contains(Macro.Offset) && !Bound.contains(SourceKey + ":" + std::to_string(Macro.Offset)))
			{
				Fail(Macro, "active marker has no supported member declaration.");
			}
		}
	}
	std::sort(Model.Apis.begin(), Model.Apis.end(), [](const auto& A, const auto& B) { return A.Id < B.Id; });
	std::set<std::string> Ids;
	for (std::size_t I = 0; I < Model.Apis.size(); ++I)
	{
		auto Id = Model.Apis[I].Id;
		std::transform(Id.begin(), Id.end(), Id.begin(), [](unsigned char C) { return std::tolower(C); });
		if (!Ids.insert(Id).second)
		{
			Fail(Model.Apis[I].Macro, "duplicate active ExportId '" + Id + "'.");
		}
		Model.Types.at(Model.Apis[I].Owner).Apis.push_back(I);
	}
	return Model;
}
} // namespace Mod::HeaderTool
