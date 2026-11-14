// Copyright (c) 2026 Nelaric Contributors
#include "ExtensionBinding.h"

#include "ModHeaderTool/src/Analysis/CursorSource.h"
#include "ModHeaderTool/src/Analysis/DeclarationBinding.h"
#include "ModHeaderTool/src/Analysis/TypeResolution.h"
#include "ModHeaderTool/src/Generation/GenerationModel.h"
#include "ModHeaderTool/src/Generation/ModuleInput.h"
#include "ModHeaderTool/src/Files/Path.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace Mod::HeaderTool
{
namespace
{
std::string Field(const ExportMacro& Macro, const char* Key)
{
	const auto It = std::ranges::find(Macro.Fields, Key, &MetadataField::Name);
	return It == Macro.Fields.end() ? std::string{} : It->Value.Text;
}

bool IsFloat(ClangLibrary& Clang, CXType Type)
{
	return Clang.clang_getCanonicalType(Type).kind == CXType_Float;
}

void BindHook(GenerationModel& Model, ClangLibrary& Clang, const Source& Source, CXCursor Member, Extension& Entry)
{
	const auto& Macro = Entry.Macro;
	if (Member.kind != CXCursor_CXXMethod)
	{
		CursorSource::Fail(Macro, "Hook must immediately precede a float member(float) declaration.");
	}
	const auto Start =
	    CursorSource::ReadLocation(Clang, Clang.clang_getRangeStart(Clang.clang_getCursorExtent(Member)));
	DeclarationBinding::CheckGap(Macro, Source, Start.Offset);
	API Signature;
	Signature.Macro = Macro;
	Signature.Module = Entry.Module;
	DeclarationBinding::BindSignature(Model, Clang, Member, Signature);
	if (Signature.Parameters.size() != 1 || Signature.Parameters[0].Type.Public != "float" ||
	    Signature.Return.Public != "float" || Signature.bConst || !Field(Macro, "ContextType").empty())
	{
		CursorSource::Fail(Macro, "this Hook backend requires one float input and a float result.");
	}
	Entry.SourceSignature = Signature.SourceSignature;
	for (const auto& Field : Macro.Fields)
	{
		for (const auto& Item : Field.Value.Items)
		{
			if (Field.Name == "AllowedStages")
			{
				Entry.bPre |= Item.Text == "Pre";
				Entry.bPost |= Item.Text == "Post";
				if (Item.Text == "On")
				{
					CursorSource::Fail(Macro, "On handlers are unsupported by the synchronous Pre/Post backend.");
				}
			}
			else if (Field.Name == "AllowedOperations")
			{
				Entry.Operations |= Item.Text == "ModifyInputs" ? 1 : Item.Text == "Cancel" ? 2 : 4;
			}
		}
	}
}

void BindEvent(ClangLibrary& Clang, const Source& Source, CXCursor Member, Extension& Entry)
{
	const auto& Macro = Entry.Macro;
	if (Member.kind != CXCursor_CXXMethod)
	{
		CursorSource::Fail(Macro, "Event requires an explicit void member(float) signature anchor.");
	}
	const auto Start =
	    CursorSource::ReadLocation(Clang, Clang.clang_getRangeStart(Clang.clang_getCursorExtent(Member)));
	DeclarationBinding::CheckGap(Macro, Source, Start.Offset);
	if (Clang.clang_getCXXAccessSpecifier(Member) != CX_CXXPublic || Clang.clang_CXXMethod_isStatic(Member) ||
	    Clang.clang_CXXMethod_isConst(Member) || Clang.clang_Cursor_isVariadic(Member) ||
	    Clang.clang_CXXMethod_isDeleted(Member) ||
	    Clang.clang_Type_getCXXRefQualifier(Clang.clang_getCursorType(Member)) != CXRefQualifier_None ||
	    Clang.clang_Cursor_getNumArguments(Member) != 1 ||
	    !IsFloat(Clang, Clang.clang_getCursorType(Clang.clang_Cursor_getArgument(Member, 0))) ||
	    Clang.clang_getCanonicalType(Clang.clang_getCursorResultType(Member)).kind != CXType_Void ||
	    Field(Macro, "SourceScope") != "World" || Field(Macro, "PublishRule") != "Immediate" ||
	    !Field(Macro, "PayloadType").empty())
	{
		CursorSource::Fail(Macro, "this Event backend requires a World/Immediate void member(float) anchor.");
	}
	Entry.SourceSignature = Entry.Owner + "::" + Entry.Name + " : " +
	                        Clang.String(Clang.clang_getTypeSpelling(Clang.clang_getCursorType(Member)));
	Entry.PublishPermission = Field(Macro, "PublishPermission");
}
} // namespace

Extension BindExtension(GenerationModel& Model, ClangLibrary& Clang, const Source& Source, ExportMacro Macro,
                        CXCursor Member)
{
	const auto Parent = Clang.clang_getCursorSemanticParent(Member);
	const auto Location = CursorSource::ReadLocation(Clang, Clang.clang_getCursorLocation(Parent), true);
	const auto* Module = TypeResolution::OwnerModule(Model.Input, Location.File);
	const auto Owner = CursorSource::QualifiedName(Clang, Parent);
	const auto Identity = Model.Input.Types.find(Owner);
	if (!Module || Identity == Model.Input.Types.end() || Identity->second.Scope != "World" ||
	    Location.File != Macro.File || (Macro.File.extension() != ".h" && Macro.File.extension() != ".hpp"))
	{
		CursorSource::Fail(Macro, "an extension requires a World type identity and a declaration in its host header.");
	}
	Extension Entry;
	Entry.Location =
	    Path::PathToUtf8(Macro.File) + ":" + std::to_string(Macro.Line) + ":" + std::to_string(Macro.Column);
	Entry.bHook = Macro.Name == "MOD_EXPORT_HOOK";
	Entry.Id = Field(Macro, "ExportId");
	Entry.Permission = Field(Macro, "Permission");
	Entry.Version = Field(Macro, "ExportVersion");
	Entry.bAuthorityOnly = Field(Macro, "Authority") != "Any";
	Entry.Name = Clang.String(Clang.clang_getCursorSpelling(Member));
	Entry.Owner = Owner;
	Entry.Module = Module->Name;
	Entry.Header = Path::PathToUtf8(Macro.File.lexically_relative(Module->Root));
	if (Entry.Owner.empty() || Entry.Name.empty() ||
	    Entry.Owner.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_:") !=
	        std::string::npos ||
	    Entry.Name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
	        std::string::npos)
	{
		CursorSource::Fail(Macro, "extension owners and members require named C++ identifiers.");
	}
	Entry.Macro = std::move(Macro);
	if (Entry.bHook)
	{
		BindHook(Model, Clang, Source, Member, Entry);
	}
	else
	{
		BindEvent(Clang, Source, Member, Entry);
	}
	return Entry;
}
} // namespace Mod::HeaderTool
