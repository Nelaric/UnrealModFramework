// Copyright (c) 2026 Nelaric Contributors
#include "TypeResolution.h"

#include "ModHeaderTool/src/Analysis/CursorSource.h"
#include "ModHeaderTool/src/Analysis/Location.h"
#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Files/Path.h"
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

ResolvedType& TypeResolution::ResolveRecord(GenerationModel& Model, ClangLibrary& Clang, CXCursor Record,
                                            const ExportMacro& Macro, const std::string& ExportModule)
{
	const auto Definition = Clang.clang_getCursorDefinition(Record);
	if (Definition.kind == CXCursor_ClassDecl || Definition.kind == CXCursor_StructDecl)
	{
		Record = Definition;
	}
	const std::string Name = CursorSource::QualifiedName(Clang, Record);
	if (auto It = Model.Types.find(Name); It != Model.Types.end())
	{
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
	const auto File =
	    fs::canonical(CursorSource::ReadLocation(Clang, Clang.clang_getCursorLocation(Record), true).File);
	const auto* Module = OwnerModule(Model.Input, File);
	const auto Engine = !Module && !ExportModule.empty() ? FindEngineHeader(Model.Input, File) : EngineHeader{};
	if ((!Module && Engine.Include.empty()) || (File.extension() != ".h" && File.extension() != ".hpp"))
	{
		CursorSource::Fail(Macro,
		                   "exported object types require a participating module or a public engine header: " + Name);
	}
	const auto Identity = Model.Input.Types.find(Name);
	if (Identity == Model.Input.Types.end() && Engine.Include.empty())
	{
		CursorSource::Fail(Macro, "missing persistent type identity for '" + Name + "'.");
	}
	ResolvedType Type;
	Type.Identity = Identity != Model.Input.Types.end() ? Identity->second : TypeIdentity{Name, "ue." + Name, "World"};
	Type.Module = Module ? Module->Name : ExportModule;
	Type.HeaderInclude = Module ? Path::PathToUtf8(File.lexically_relative(Module->Root)) : Engine.Include;
	Type.Symbol = "T_" + Hash::HashBytes(Type.Identity.Id).substr(0, 16);
	Type.MethodNames = std::move(Evidence.MethodNames);
	Type.bAllowWorldless = !Engine.Include.empty();
	Visited.clear();
	Type.bLocalPlayerSubsystem = IsUObject(Clang, Record, Visited, "ULocalPlayerSubsystem");
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
			const auto Relative = File.lexically_relative(Owner->Root);
			Result.HostInclude = !Engine.Include.empty() ? Engine.Include : Path::PathToUtf8(Relative);
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
