// Copyright (c) 2026 Nelaric Contributors
#include "FullViewSelection.h"

#include "ModHeaderTool/src/Analysis/CursorSource.h"
#include "ModHeaderTool/src/Analysis/DeclarationBinding.h"
#include "ModHeaderTool/src/Analysis/TypeResolution.h"
#include "ModHeaderTool/src/Generation/APIType.h"
#include "ModHeaderTool/src/Generation/ResolvedType.h"
#include "ModHeaderTool/src/Generation/TypeIdentity.h"
#include "ModHeaderTool/src/Hash/Hash.h"
#include "ModHeaderTool/src/Metadata/MetadataField.h"
#include "ModHeaderTool/src/Metadata/MetadataValue.h"
#include "ModHeaderTool/src/Scan/ExportMacro.h"

#include <algorithm>
#include <clang-c/Index.h>
#include <exception>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Mod::HeaderTool
{
std::string FullViewSelection::MemberKey(ClangLibrary& Clang, CXCursor Method)
{
	std::string Key = Clang.String(Clang.clang_getCursorSpelling(Method)) + "(";
	for (int I = 0; I < Clang.clang_Cursor_getNumArguments(Method); ++I)
	{
		if (I != 0)
		{
			Key += ", ";
		}
		const auto Argument = Clang.clang_Cursor_getArgument(Method, I);
		Key += Clang.String(
		    Clang.clang_getTypeSpelling(Clang.clang_getCanonicalType(Clang.clang_getCursorType(Argument))));
	}
	const auto Ref = Clang.clang_Type_getCXXRefQualifier(Clang.clang_getCursorType(Method));
	return Key + ")" + (Clang.clang_CXXMethod_isConst(Method) ? " const" : "") +
	       (Ref == CXRefQualifier_LValue   ? " &"
	        : Ref == CXRefQualifier_RValue ? " &&"
	                                       : "");
}

void FullViewSelection::CollectPublicMembers(ClangLibrary& Clang, CXCursor Record,
                                             std::map<std::string, CXCursor>& Members, std::set<std::string>& Visited)
{
	Record = Clang.clang_getCursorDefinition(Record);
	if ((Record.kind != CXCursor_ClassDecl && Record.kind != CXCursor_StructDecl) ||
	    !Visited.insert(Clang.String(Clang.clang_getCursorUSR(Record))).second)
	{
		return;
	}
	struct Children
	{
		ClangLibrary& Clang;
		std::map<std::string, CXCursor>& Members;
		std::vector<CXCursor> Bases;
	} ChildrenContext{Clang, Members, {}};
	Clang.clang_visitChildren(
	    Record,
	    [](CXCursor Child, CXCursor, CXClientData Data)
	    {
		    auto& Context = *static_cast<Children*>(Data);
		    auto& Clang = Context.Clang;
		    if (Clang.clang_getCXXAccessSpecifier(Child) == CX_CXXPublic)
		    {
			    if (Child.kind == CXCursor_CXXBaseSpecifier)
			    {
				    Context.Bases.push_back(
				        Clang.clang_getTypeDeclaration(Clang.clang_getCanonicalType(Clang.clang_getCursorType(Child))));
			    }
			    else if ((Child.kind == CXCursor_CXXMethod || Child.kind == CXCursor_ConversionFunction) &&
			             !Clang.clang_CXXMethod_isStatic(Child) && !Clang.clang_CXXMethod_isDeleted(Child))
			    {
				    Context.Members.try_emplace(MemberKey(Clang, Child), Child);
			    }
		    }
		    return CXChildVisit_Continue;
	    },
	    &ChildrenContext);
	for (const auto Base : ChildrenContext.Bases)
	{
		CollectPublicMembers(Clang, Base, Members, Visited);
	}
}

void FullViewSelection::SelectFullViews(GenerationModel& Model, ClangLibrary& Clang, CXCursor Method,
                                        const API& Exported, std::map<std::string, FullView>& FullViews)
{
	const auto Field = std::find_if(Exported.Macro.Fields.begin(), Exported.Macro.Fields.end(),
	                                [](const auto& Value) { return Value.Name == "FullView"; });
	if (Field == Exported.Macro.Fields.end())
	{
		return;
	}
	for (const auto& Item : Field->Value.Items)
	{
		const std::string Name = Item.Text.starts_with("::") ? Item.Text.substr(2) : Item.Text;
		const auto Parameter =
		    std::find_if(Exported.Parameters.begin(), Exported.Parameters.end(), [&](const auto& Param)
		                 { return Param.Type.Kind == APIType::EKind::ObjectInput && Param.Type.Object == Name; });
		if (Parameter == Exported.Parameters.end())
		{
			CursorSource::Fail(Exported.Macro, "FullView class '" + Name + "' is not a UObject pointer parameter of '" +
			                                       Exported.Name + "'.");
		}
		const auto [View, bNew] = FullViews.try_emplace(Name);
		View->second.Permissions.insert(Exported.Permissions.begin(), Exported.Permissions.end());
		if (!bNew)
		{
			continue;
		}
		const auto Argument =
		    Clang.clang_Cursor_getArgument(Method, static_cast<unsigned>(Parameter - Exported.Parameters.begin()));
		const auto Pointer = Clang.clang_getCanonicalType(Clang.clang_getCursorType(Argument));
		const auto Record =
		    Clang.clang_getTypeDeclaration(Clang.clang_getCanonicalType(Clang.clang_getPointeeType(Pointer)));
		auto& Type = TypeResolution::ResolveRecord(Model, Clang, Record, Exported.Macro, Exported.Module);
		Type.bFullView = true;
		Type.bAllowWorldless = true;
		std::map<std::string, CXCursor> Members;
		std::set<std::string> Visited;
		CollectPublicMembers(Clang, Record, Members, Visited);
		std::ostringstream Errors;
		for (const auto& [Key, Member] : Members)
		{
			API Export;
			Export.Macro = Exported.Macro;
			Export.Macro.Fields.clear();
			Export.Module = Type.Module;
			Export.Owner = Name;
			Export.Scope = Type.Identity.Scope;
			Export.Version = "1.0.0";
			Export.bFullViewMember = true;
			try
			{
				DeclarationBinding::BindSignature(Model, Clang, Member, Export);
				Export.Id = Type.Identity.Id + ".FullView." + Export.Name + "." + Hash::HashBytes(Key).substr(0, 16);
				Export.Symbol = "A_" + Hash::HashBytes(Export.Id).substr(0, 16);
				View->second.Members.push_back(std::move(Export));
			}
			catch (const std::exception& Error)
			{
				std::string_view Reason = Error.what();
				constexpr std::string_view Separator = "): error: MOD_EXPORT_API: ";
				if (const auto Start = Reason.find(Separator); Start != Reason.npos)
				{
					Reason.remove_prefix(Start + Separator.size());
				}
				Errors << "\n  " << CursorSource::QualifiedName(Clang, Clang.clang_getCursorSemanticParent(Member))
				       << "::" << Key << ": " << Reason;
			}
		}
		if (Errors.tellp() > 0)
		{
			CursorSource::Fail(Exported.Macro, "cannot generate complete FullView for '" + Name + "':" + Errors.str());
		}
	}
}
} // namespace Mod::HeaderTool
