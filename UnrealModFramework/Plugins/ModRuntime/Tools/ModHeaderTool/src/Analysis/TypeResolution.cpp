// Copyright (c) 2026 Nelaric Contributors
#include "TypeResolution.h"

#include "ModHeaderTool/src/Analysis/CursorSource.h"
#include "ModHeaderTool/src/Analysis/Location.h"
#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Generation/GenerationFields.h"
#include "ModHeaderTool/src/Generation/TypeIdentity.h"
#include "ModHeaderTool/src/Hash/Hash.h"

#include <algorithm>
#include <clang-c/Index.h>
#include <filesystem>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

bool TypeResolution::IsUObject(ClangLibrary& Clang, CXCursor Record, std::set<std::string>& Visited,
                               std::string_view BaseName)
{
	const auto Definition = Clang.clang_getCursorDefinition(Record);
	if (Definition.kind == CXCursor_ClassDecl || Definition.kind == CXCursor_StructDecl)
	{
		Record = Definition;
	}
	if (CursorSource::QualifiedName(Clang, Record) == BaseName)
	{
		return true;
	}
	if (!Visited.insert(Clang.String(Clang.clang_getCursorUSR(Record))).second)
	{
		return false;
	}
	struct BaseState
	{
		ClangLibrary* Clang;
		std::vector<CXCursor> Bases;
	} Bases{&Clang, {}};
	Clang.clang_visitChildren(
	    Record,
	    [](CXCursor Child, CXCursor, CXClientData Data)
	    {
		    auto& State = *static_cast<BaseState*>(Data);
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

const ModuleInput* TypeResolution::OwnerModule(const GenerationInput& Input, const fs::path& File)
{
	const ModuleInput* Result = nullptr;
	for (const auto& Module : Input.Modules)
	{
		const auto Relative = File.lexically_relative(Module.Root);
		if (!Relative.empty() && *Relative.begin() != "..")
		{
			if (Result)
			{
				throw std::runtime_error("Overlapping source modules: " +
				                         DiagnosticLocation::QuoteDiagnosticPath(File));
			}
			Result = &Module;
		}
	}
	return Result;
}

EngineHeader TypeResolution::FindEngineHeader(const GenerationInput& Input, const fs::path& File)
{
	const auto Root = Input.PathRoots.find("Engine");
	if (Root == Input.PathRoots.end())
	{
		return {};
	}
	const auto Relative = File.lexically_relative(Root->second);
	if (Relative.empty() || *Relative.begin() == "..")
	{
		return {};
	}
	fs::path Previous;
	for (auto Part = Relative.begin(); Part != Relative.end(); ++Part)
	{
		if ((*Part == "Public" || *Part == "Classes") && !Previous.empty())
		{
			fs::path Include;
			for (auto Name = std::next(Part); Name != Relative.end(); ++Name)
			{
				Include /= *Name;
			}
			return {Path::PathToUtf8(Previous), Path::PathToUtf8(Include)};
		}
		Previous = *Part;
	}
	return {};
}

std::string TypeResolution::InferExportScope(ClangLibrary& Clang, CXCursor Record, const ExportMacro& Macro)
{
	std::set<std::string> Visited;
	if (IsUObject(Clang, Record, Visited, "ULocalPlayerSubsystem"))
	{
		return "Player";
	}
	for (const auto* WorldBase : {"AActor", "UActorComponent", "UWorldSubsystem"})
	{
		Visited.clear();
		if (IsUObject(Clang, Record, Visited, WorldBase))
		{
			return "World";
		}
	}
	CursorSource::Fail(Macro, "declare Scope for a type without an unambiguous framework lifetime.");
}

TypeIdentity TypeResolution::DiscoverIdentity(GenerationModel& Model, ClangLibrary& Clang, CXCursor Record,
                                              const ExportMacro& Macro, const fs::path& File, std::string_view Module,
                                              std::string_view Scope, bool bEngine)
{
	const std::string Name = CursorSource::QualifiedName(Clang, Record);
	if (Name.empty() || Module.empty())
	{
		CursorSource::Fail(Macro, "a type identity requires an owning module and qualified C++ name.");
	}
	const auto Reflected = Model.Input.Reflection.find({Clang.String(Clang.clang_getCursorSpelling(Record)), File});
	std::string Id;
	if (Reflected != Model.Input.Reflection.end())
	{
		const auto& Reflection = Reflected->second;
		if (!bEngine && Module != Reflection.Module)
		{
			CursorSource::Fail(Macro, "the UHT and compilation module disagree for '" + Name + "'.");
		}
		Id = Reflection.LegacyId.empty() ? "ue." + GenerationFields::Fold(Reflection.Path.substr(8))
		                                 : Reflection.LegacyId;
	}
	if (bEngine)
	{
		// Preserve the published engine views on both source and installed engine targets.
		Id = "ue." + Name;
	}
	if (Id.empty())
	{
		std::string Qualified = Name;
		for (auto Index = Qualified.find("::"); Index != Qualified.npos; Index = Qualified.find("::", Index + 1))
		{
			Qualified.replace(Index, 2, ".");
		}
		Id = "cpp." + GenerationFields::Fold(std::string(Module) + "." + Qualified);
	}
	TypeIdentity Identity{Name, std::move(Id), std::string(Scope)};
	const auto [Existing, bNew] =
	    Model.IdentityOwners.emplace(GenerationFields::Fold(Identity.Id), std::pair{std::string(Module), Identity});
	if (!bNew && (Existing->second.first != Module || Existing->second.second.CppName != Name))
	{
		CursorSource::Fail(Macro, "type identity '" + Identity.Id + "' is already used by " + Existing->second.first +
		                              "::" + Existing->second.second.CppName + ".");
	}
	if (!bNew && Existing->second.second.Scope != Scope)
	{
		CursorSource::Fail(Macro, "conflicting export scopes for '" + Name + "': " + Existing->second.second.Scope +
		                              " and " + std::string(Scope) + ".");
	}
	return Identity;
}

ResolvedType& TypeResolution::ResolveRecord(GenerationModel& Model, ClangLibrary& Clang, CXCursor Record,
                                            const ExportMacro& Macro, const std::string& ExportModule,
                                            std::string_view DeclaredScope)
{
	const auto Definition = Clang.clang_getCursorDefinition(Record);
	if (Definition.kind == CXCursor_ClassDecl || Definition.kind == CXCursor_StructDecl)
	{
		Record = Definition;
	}
	const std::string Name = CursorSource::QualifiedName(Clang, Record);
	const auto File =
	    fs::canonical(CursorSource::ReadLocation(Clang, Clang.clang_getCursorLocation(Record), true).File);
	if (auto It = Model.Types.find(Name); It != Model.Types.end())
	{
		if (It->second.DeclarationFile != File)
		{
			CursorSource::Fail(Macro, "ambiguous C++ type '" + Name + "' has multiple definitions in this target.");
		}
		if (!DeclaredScope.empty() && It->second.Identity.Scope != DeclaredScope)
		{
			CursorSource::Fail(Macro, "conflicting export scopes for '" + Name + "': " + It->second.Identity.Scope +
			                              " and " + std::string(DeclaredScope) + ".");
		}
		return It->second;
	}
	std::set<std::string> Visited;
	if ((Record.kind != CXCursor_ClassDecl && Record.kind != CXCursor_StructDecl) || !IsUObject(Clang, Record, Visited))
	{
		CursorSource::Fail(Macro, "the UObject backend requires a concrete UObject-derived class: " + Name);
	}
	struct ReflectionEvidence
	{
		ClangLibrary* Clang;
		std::set<std::string> MethodNames;
		bool bOwnStaticClass = false;
	} Evidence{&Clang, {}};
	Clang.clang_visitChildren(
	    Record,
	    [](CXCursor Child, CXCursor, CXClientData Data)
	    {
		    auto& Evidence = *static_cast<ReflectionEvidence*>(Data);
		    if (Child.kind == CXCursor_CXXMethod)
		    {
			    const auto Name = Evidence.Clang->String(Evidence.Clang->clang_getCursorSpelling(Child));
			    Evidence.MethodNames.insert(Name);
			    Evidence.bOwnStaticClass |= Name == "StaticClass" && Evidence.Clang->clang_CXXMethod_isStatic(Child);
		    }
		    return CXChildVisit_Continue;
	    },
	    &Evidence);
	if (!Evidence.bOwnStaticClass)
	{
		CursorSource::Fail(Macro,
		                   "exported UObject types require their own generated StaticClass declaration: " + Name);
	}
	const auto* Module = OwnerModule(Model.Input, File);
	const auto Engine = !Module && !ExportModule.empty() ? FindEngineHeader(Model.Input, File) : EngineHeader{};
	std::string Include = Module ? Module->HeaderInclude(File) : Engine.Include;
	if (Include.empty() && Model.Input.bAutoDiscoverModules && !ExportModule.empty())
	{
		// An exported signature may use a public UObject from an ordinary UE dependency.
		// Resolve its included header without scanning that module's own declarations.
		for (auto Directory = File.parent_path(); Directory != Directory.root_path();
		     Directory = Directory.parent_path())
		{
			if ((Directory.filename() == "Public" || Directory.filename() == "Classes") &&
			    std::ranges::any_of(fs::directory_iterator(Directory.parent_path()),
			                        [](const fs::directory_entry& Entry) {
				                        return Entry.is_regular_file() &&
				                               Path::PathToUtf8(Entry.path().filename()).ends_with(".Build.cs");
			                        }))
			{
				Include = Path::PathToUtf8(File.lexically_relative(Directory));
				break;
			}
		}
	}
	if (Include.empty() || (File.extension() != ".h" && File.extension() != ".hpp"))
	{
		CursorSource::Fail(Macro,
		                   "exported object types require a participating module or a public engine header: " + Name);
	}
	const auto Reflected = Model.Input.Reflection.find({Clang.String(Clang.clang_getCursorSpelling(Record)), File});
	ResolvedType Type;
	Type.DeclarationFile = File;
	Type.Module = Module ? Module->Name : ExportModule;
	Type.HeaderInclude = std::move(Include);
	Type.MethodNames = std::move(Evidence.MethodNames);
	Type.bAllowWorldless = !Engine.Include.empty();
	Visited.clear();
	Type.bLocalPlayerSubsystem = IsUObject(Clang, Record, Visited, "ULocalPlayerSubsystem");
	const std::string Scope =
	    DeclaredScope.empty() ? (Type.bLocalPlayerSubsystem ? "Player" : "World") : std::string(DeclaredScope);
	if (Type.bLocalPlayerSubsystem && Scope != "Player")
	{
		CursorSource::Fail(Macro, "a LocalPlayer subsystem requires Player scope: " + Name);
	}
	if (Reflected != Model.Input.Reflection.end())
	{
		Type.ReflectedPath = Reflected->second.Path;
	}
	const auto LogicalModule = Module                                      ? Module->Name
	                           : !Engine.Module.empty()                    ? Engine.Module
	                           : Reflected != Model.Input.Reflection.end() ? Reflected->second.Module
	                                                                       : Type.Module;
	Type.Identity = DiscoverIdentity(Model, Clang, Record, Macro, File, LogicalModule, Scope, !Engine.Include.empty());
	Type.Symbol = "T_" + Hash::HashBytes(Type.Identity.Id).substr(0, 16);
	return Model.Types.emplace(Name, std::move(Type)).first->second;
}

APIType TypeResolution::ResolveType(GenerationModel& Model, ClangLibrary& Clang, CXType Type, const API& API,
                                    bool bInput)
{
	Type = Clang.clang_getCanonicalType(Type);
	APIType Result;
	Result.Cpp = Clang.String(Clang.clang_getTypeSpelling(Type));
	if (Type.kind == CXType_Void)
	{
		Result.Public = "void";
		return Result;
	}
	Result.Kind = APIType::EKind::Scalar;
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
		const auto Declaration = Clang.clang_getTypeDeclaration(Pointee);
		if (bInput)
		{
			std::set<std::string> Visited;
			const auto Definition = Clang.clang_getCursorDefinition(Declaration);
			if ((Definition.kind != CXCursor_ClassDecl && Definition.kind != CXCursor_StructDecl) ||
			    !IsUObject(Clang, Definition, Visited))
			{
				CursorSource::Fail(API.Macro, "input '" + Result.Cpp + "' requires a complete UObject class.");
			}
			const auto File =
			    fs::canonical(CursorSource::ReadLocation(Clang, Clang.clang_getCursorLocation(Definition), true).File);
			const auto Engine = FindEngineHeader(Model.Input, File);
			Result.Kind = APIType::EKind::ObjectInput;
			Result.Object = CursorSource::QualifiedName(Clang, Definition);
			Result.bConst = Clang.clang_isConstQualifiedType(Pointee) != 0;
			Result.Public = std::string(Result.bConst ? "const " : "") + Result.Object + "*";
			Result.EngineModule = Engine.Module;
			Result.EngineInclude = Engine.Include;
			const auto Owner = std::find_if(Model.Input.Modules.begin(), Model.Input.Modules.end(),
			                                [&](const auto& Module) { return Module.Name == API.Module; });
			const auto* DeclaringModule = OwnerModule(Model.Input, File);
			Result.HostInclude = !Engine.Include.empty() ? Engine.Include
			                                             : (DeclaringModule ? DeclaringModule->HeaderInclude(File)
			                                                                : Owner->HeaderInclude(File));
			if (Result.HostInclude.empty())
			{
				CursorSource::Fail(API.Macro, "cannot include the declaration of input class '" + Result.Object +
				                                  "' at " + DiagnosticLocation::QuoteDiagnosticPath(File) + ".");
			}
			break;
		}
		auto& Record = ResolveRecord(Model, Clang, Declaration, API.Macro, API.Module);
		if (Record.Identity.Scope != "World")
		{
			CursorSource::Fail(API.Macro, "returned objects require a World-scoped type.");
		}
		Result.Kind = APIType::EKind::Object;
		Result.Object = Record.Identity.CppName;
		Result.bConst = Clang.clang_isConstQualifiedType(Pointee) != 0;
		Result.Public = "FModRuntimeObject<" + std::string(Result.bConst ? "const " : "") + Result.Object + ">";
		break;
	}
	default:
		CursorSource::Fail(API.Macro, "no calling projection for type '" + Result.Cpp + "'.");
	}
	return Result;
}
} // namespace Mod::HeaderTool
