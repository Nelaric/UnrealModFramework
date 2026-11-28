// Copyright (c) 2026 Nelaric Contributors
#include "ExtensionBinding.h"

#include "ModHeaderTool/src/Analysis/CursorSource.h"
#include "ModHeaderTool/src/Analysis/DeclarationBinding.h"
#include "ModHeaderTool/src/Analysis/TypeResolution.h"
#include "ModHeaderTool/src/Generation/GenerationModel.h"
#include "ModHeaderTool/src/Generation/ModuleInput.h"
#include "ModHeaderTool/src/Files/Path.h"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <vector>
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

std::string HookSlug(std::string_view Name)
{
	std::string Result;
	for (std::size_t Index = 0; Index < Name.size(); ++Index)
	{
		const unsigned char Letter = Name[Index];
		const bool bWordStart =
		    Index > 0 && std::isupper(Letter) &&
		    (std::islower(static_cast<unsigned char>(Name[Index - 1])) ||
		     (Index + 1 < Name.size() && std::islower(static_cast<unsigned char>(Name[Index + 1]))));
		if (bWordStart && Result.back() != '-')
		{
			Result += '-';
		}
		Result += Letter == '_' ? '-' : static_cast<char>(std::tolower(Letter));
	}
	return Result;
}

std::string StructLayout(ClangLibrary& Clang, CXType Type, int Depth, int& Count)
{
	if (Depth == 8 || Type.kind != CXType_Record)
	{
		return {};
	}
	struct FWalk
	{
		ClangLibrary* Clang;
		std::vector<CXCursor> Fields;
		bool HasStaticStruct = false;
		bool HasBase = false;
	} Walk{&Clang, {}};
	const auto Record = Clang.clang_getTypeDeclaration(Type);
	Clang.clang_visitChildren(
	    Record,
	    [](CXCursor Cursor, CXCursor, CXClientData Data)
	    {
		    auto& Walk = *static_cast<FWalk*>(Data);
		    if (Cursor.kind == CXCursor_FieldDecl)
		    {
			    Walk.Fields.push_back(Cursor);
		    }
		    Walk.HasBase |= Cursor.kind == CXCursor_CXXBaseSpecifier;
		    Walk.HasStaticStruct |= Cursor.kind == CXCursor_CXXMethod &&
		                            Walk.Clang->String(Walk.Clang->clang_getCursorSpelling(Cursor)) == "StaticStruct";
		    return CXChildVisit_Continue;
	    },
	    &Walk);
	if (!Walk.HasStaticStruct || Walk.HasBase || Walk.Fields.empty())
	{
		return {};
	}
	std::string Layout = "{";
	for (const auto Field : Walk.Fields)
	{
		if (++Count > 128 || Clang.clang_getCXXAccessSpecifier(Field) != CX_CXXPublic)
		{
			return {};
		}
		const auto FieldType = Clang.clang_getCanonicalType(Clang.clang_getCursorType(Field));
		std::string Shape;
		switch (FieldType.kind)
		{
		case CXType_Bool:
			Shape = "BoolProperty";
			break;
		case CXType_Float:
			Shape = "FloatProperty";
			break;
		case CXType_Double:
			Shape = "DoubleProperty";
			break;
		case CXType_Int:
			Shape = "IntProperty";
			break;
		case CXType_LongLong:
			Shape = "Int64Property";
			break;
		case CXType_Char_U:
		case CXType_UChar:
			Shape = "ByteProperty";
			break;
		case CXType_Record:
			Shape = StructLayout(Clang, FieldType, Depth + 1, Count);
			break;
		default:
			return {};
		}
		if (Shape.empty())
		{
			return {};
		}
		Layout += Clang.String(Clang.clang_getCursorSpelling(Field)) + ":" + Shape + ";";
	}
	return Layout + "}";
}

bool BindProjected(GenerationModel& Model, ClangLibrary& Clang, CXCursor Member, Extension& Entry)
{
	const auto& Macro = Entry.Macro;
	if (Clang.clang_Cursor_getNumArguments(Member) != 1)
	{
		return false;
	}
	const auto ReadType = [&](CXType Type)
	{
		Type = Clang.clang_getCanonicalType(Type);
		return Type.kind == CXType_LValueReference ? Clang.clang_getCanonicalType(Clang.clang_getPointeeType(Type))
		                                           : Type;
	};
	const auto InputCursor = Clang.clang_Cursor_getArgument(Member, 0);
	const auto Input = ReadType(Clang.clang_getCursorType(InputCursor));
	if (Input.kind != CXType_Record && Input.kind != CXType_Pointer)
	{
		return false;
	}
	const auto Result = Clang.clang_getCanonicalType(Clang.clang_getCursorResultType(Member));
	const auto Name = [&](CXType Type)
	{
		if (Type.kind == CXType_Pointer)
		{
			Type = Clang.clang_getCanonicalType(Clang.clang_getPointeeType(Type));
		}
		return CursorSource::QualifiedName(Clang, Clang.clang_getTypeDeclaration(Type));
	};
	const auto PayloadName = Name(Input);
	if (PayloadName.empty() || Clang.clang_getCXXAccessSpecifier(Member) != CX_CXXPublic ||
	    Clang.clang_CXXMethod_isStatic(Member) || Clang.clang_CXXMethod_isConst(Member) ||
	    Clang.clang_Cursor_isVariadic(Member) || Clang.clang_CXXMethod_isDeleted(Member) ||
	    Clang.clang_Type_getCXXRefQualifier(Clang.clang_getCursorType(Member)) != CXRefQualifier_None ||
	    Result.kind != CXType_Void)
	{
		CursorSource::Fail(Macro, "expected a public one-payload World member with a matching Hook result.");
	}
	const auto Requested = Field(Macro, "PayloadType");
	if (!Requested.empty() && Requested != PayloadName)
	{
		CursorSource::Fail(Macro, "the reflected payload type must match the declared member.");
	}
	Entry.PayloadCpp = PayloadName;
	Entry.SourceSignature = Clang.String(Clang.clang_getTypeSpelling(Clang.clang_getCursorType(Member)));
	if (Input.kind == CXType_Record)
	{
		const auto Original = Clang.clang_getCanonicalType(Clang.clang_getCursorType(InputCursor));
		if (Original.kind == CXType_LValueReference && !Clang.clang_isConstQualifiedType(Input))
		{
			CursorSource::Fail(Macro, "struct inputs require a value or a const reference.");
		}
		int FieldCount = 0;
		Entry.PayloadLayout = StructLayout(Clang, Input, 0, FieldCount);
		if (Entry.PayloadLayout.empty())
		{
			CursorSource::Fail(Macro, "struct payload fields must be reflected scalars or nested value structs.");
		}
		Entry.PayloadKind = "Struct";
	}
	else
	{
		const auto Class =
		    Clang.clang_getTypeDeclaration(Clang.clang_getCanonicalType(Clang.clang_getPointeeType(Input)));
		auto& Type = TypeResolution::ResolveRecord(Model, Clang, Class, Macro, Entry.Module);
		if (Type.Identity.Scope != "World")
		{
			CursorSource::Fail(Macro, "object payloads require a generated World view.");
		}
		Entry.PayloadKind = "Object";
		Entry.PayloadTypeId = Type.Identity.Id;
		Entry.PayloadHeader = Type.HeaderInclude;
	}
	return true;
}

void BindHook(GenerationModel& Model, ClangLibrary& Clang, const Source& Source, CXCursor Member, Extension& Entry)
{
	const auto& Macro = Entry.Macro;
	if (Member.kind != CXCursor_CXXMethod)
	{
		CursorSource::Fail(Macro, "Hook must immediately precede its member declaration.");
	}
	const auto Start =
	    CursorSource::ReadLocation(Clang, Clang.clang_getRangeStart(Clang.clang_getCursorExtent(Member)));
	DeclarationBinding::CheckGap(Macro, Source, Start.Offset);
	API Signature;
	Signature.Macro = Macro;
	Signature.Module = Entry.Module;
	if (Clang.clang_isCursorDefinition(Member))
	{
		CursorSource::Fail(Macro, "automatic Hooks require an out-of-line scalar member declaration.");
	}
	DeclarationBinding::BindSignature(Model, Clang, Member, Signature);
	if (Signature.Parameters.size() != 1 || !IsExtensionScalar(Signature.Return.Public) ||
	    Signature.Parameters[0].Type.Public != Signature.Return.Public || Signature.bConst)
	{
		CursorSource::Fail(Macro, "this Hook backend requires one scalar input and a result of the same type.");
	}
	Entry.Scalar = Signature.Return.Public;
	Entry.SourceSignature = std::move(Signature.SourceSignature);
	Entry.DefinitionUsr = std::move(Signature.DeclarationUsr);
	if (Entry.DefinitionUsr.empty())
	{
		CursorSource::Fail(Macro, "cannot identify the automatic Hook's C++ declaration.");
	}
	for (const auto& Field : Macro.Fields)
	{
		for (const auto& Item : Field.Value.Items)
		{
			if (Field.Name == "AllowedStages")
			{
				Entry.bPre |= Item.Text == "Pre";
				Entry.bPost |= Item.Text == "Post";
				Entry.bOn |= Item.Text == "On";
			}
			else if (Field.Name == "AllowedOperations")
			{
				constexpr std::string_view Operations[]{"ModifyInputs", "Cancel", "ModifyResult", "Wrap", "Replace"};
				const auto Operation = std::ranges::find(Operations, Item.Text);
				if (Operation == std::end(Operations))
				{
					CursorSource::Fail(Macro, "unsupported Hook operation.");
				}
				Entry.Operations |= static_cast<std::uint8_t>(1u << std::distance(std::begin(Operations), Operation));
			}
		}
	}
}

void BindEvent(GenerationModel& Model, ClangLibrary& Clang, const Source& Source, CXCursor Member, Extension& Entry)
{
	const auto& Macro = Entry.Macro;
	if (Member.kind != CXCursor_CXXMethod)
	{
		CursorSource::Fail(Macro, "Event requires an explicit member signature anchor.");
	}
	const auto Start =
	    CursorSource::ReadLocation(Clang, Clang.clang_getRangeStart(Clang.clang_getCursorExtent(Member)));
	DeclarationBinding::CheckGap(Macro, Source, Start.Offset);
	API Signature;
	Signature.Macro = Macro;
	Signature.Module = Entry.Module;
	if (!BindProjected(Model, Clang, Member, Entry))
	{
		DeclarationBinding::BindSignature(Model, Clang, Member, Signature);
	}
	const auto PublishRule = Field(Macro, "PublishRule");
	if (Field(Macro, "SourceScope") != "World" || (PublishRule != "Immediate" && PublishRule != "NextTick") ||
	    (Entry.PayloadKind.empty() &&
	     (Signature.bConst || Signature.Parameters.size() != 1 ||
	      !IsExtensionScalar(Signature.Parameters[0].Type.Public) || Signature.Return.Kind != APIType::EKind::Void ||
	      !Field(Macro, "PayloadType").empty())))
	{
		CursorSource::Fail(Macro, "this Event backend requires a World void member with a single scalar argument.");
	}
	Entry.bQueued = PublishRule == "NextTick";
	Entry.bImmediateUnsubscribe = Field(Macro, "UnsubscribeRule") == "Immediate";
	if (Entry.PayloadKind.empty())
	{
		Entry.Scalar = Signature.Parameters[0].Type.Public;
		Entry.SourceSignature = Signature.SourceSignature;
	}
	Entry.PublishPermission = Field(Macro, "PublishPermission");
}

void BindEventHostMember(ClangLibrary& Clang, CXCursor Member, Extension& Entry)
{
	const auto& Macro = Entry.Macro;
	auto& Host = Entry.HostMember;
	Host.Endpoint = Field(Macro, "HostEndpoint");
	if (Host.Endpoint.empty())
	{
		return;
	}
	const auto FunctionType = Clang.clang_getCanonicalType(Clang.clang_getCursorType(Member));
	const int Exception = Clang.clang_getExceptionSpecificationType(FunctionType);
	if (Clang.clang_isCursorDefinition(Member) ||
	    Clang.String(Clang.clang_getTypeSpelling(FunctionType)).find("volatile") != std::string::npos ||
	    (Exception != CXCursor_ExceptionSpecificationKind_None &&
	     Exception != CXCursor_ExceptionSpecificationKind_BasicNoexcept))
	{
		CursorSource::Fail(Macro,
		                   "'HostEndpoint' requires a declaration without a body, volatile, or computed noexcept.");
	}
	const auto TypeSpelling = [&](CXType Type)
	{ return Clang.String(Clang.clang_getTypeSpelling(Clang.clang_getCanonicalType(Type))); };
	Host.ParameterType = TypeSpelling(Clang.clang_getCursorType(Clang.clang_Cursor_getArgument(Member, 0)));
	Host.ReturnType = TypeSpelling(Clang.clang_getCursorResultType(Member));
	Host.bNoexcept = Exception == CXCursor_ExceptionSpecificationKind_BasicNoexcept;
	Host.CommitPayload = Field(Macro, "HostCommitPayload");
}
} // namespace

void BindExtension(GenerationModel& Model, ClangLibrary& Clang, const Source& Source, ExportMacro Macro,
                   CXCursor Member)
{
	const auto Parent = Clang.clang_getCursorSemanticParent(Member);
	const auto Location = CursorSource::ReadLocation(Clang, Clang.clang_getCursorLocation(Parent), true);
	const auto* Module = TypeResolution::OwnerModule(Model.Input, Location.File);
	const auto Owner = CursorSource::QualifiedName(Clang, Parent);
	if (!Module || Location.File != Macro.File || (Macro.File.extension() != ".h" && Macro.File.extension() != ".hpp"))
	{
		CursorSource::Fail(Macro, "an extension requires a declaration in its host header.");
	}
	const auto Identity =
	    TypeResolution::DiscoverIdentity(Model, Clang, Parent, Macro, Location.File, Module->Name, "World", false);
	Extension Entry;
	Entry.Location =
	    Path::PathToUtf8(Macro.File) + ":" + std::to_string(Macro.Line) + ":" + std::to_string(Macro.Column);
	Entry.bHook = Macro.Name == "MOD_EXPORT_HOOK";
	Entry.Version = Field(Macro, "ExportVersion");
	Entry.bAuthorityOnly = Field(Macro, "Authority") != "Any";
	Entry.Name = Clang.String(Clang.clang_getCursorSpelling(Member));
	Entry.Owner = Owner;
	Entry.Module = Module->Name;
	Entry.Header = Module->HeaderInclude(Macro.File);
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
		if (!Entry.bPre && !Entry.bOn && !Entry.bPost)
		{
			return;
		}
		const auto Prefix = "hook/" + Identity.Id + "/";
		Entry.Id = Prefix + HookSlug(Entry.Name);
		Entry.Permission = Entry.Id;
		const auto Names = std::ranges::find(Entry.Macro.Fields, "CompatibleNames", &MetadataField::Name);
		if (Names != Entry.Macro.Fields.end())
		{
			for (const auto& OldName : Names->Value.Items)
			{
				Extension Alias = Entry;
				Alias.Name = OldName.Text;
				Alias.Id = Prefix + HookSlug(Alias.Name);
				Alias.Permission = Alias.Id;
				Alias.DispatchId = Entry.Id;
				Alias.DefinitionUsr.clear();
				AddExtension(Model, std::move(Alias));
			}
		}
	}
	else
	{
		Entry.Id = Field(Entry.Macro, "ExportId");
		Entry.Permission = Field(Entry.Macro, "Permission");
		BindEvent(Model, Clang, Source, Member, Entry);
		BindEventHostMember(Clang, Member, Entry);
	}
	AddExtension(Model, std::move(Entry));
}
} // namespace Mod::HeaderTool
