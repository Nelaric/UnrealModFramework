// Copyright (c) 2026 Nelaric Contributors
/** @file ModExportMetadataSchema.h */
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace Mod::ExportMetadata
{
namespace Schema
{
/// Version of the resolved UE metadata option snapshot.
inline constexpr int SnapshotVersion = 5;
/// Default location relative to the project root.
inline constexpr const char* SnapshotPath = "Intermediate/ModTools/ModHeaderTool/MetadataOptions.json";
/// Default declaration export version.
inline constexpr const char* DefaultExportVersion = "1.0.0";
/// Default stage for a read-only Hook declaration.
inline constexpr const char* DefaultHookStage = "Post";
/// Supported Hook stages in the metadata contract.
inline constexpr std::array<std::string_view, 3> HookStages = {"Pre", "On", "Post"};
/// Authority policies for synchronous World extensions.
inline constexpr std::array<std::string_view, 2> ExtensionAuthorities = {"Server", "Any"};

/// A Hook operation and the stage that must be open to allow it.
struct FHookOperationSpec
{
	/// Operation name in the declaration.
	const char* Name;
	/// Required stage name in the declaration.
	const char* Stage;
};

/// Operations require an explicit grant in each Hook declaration.
inline constexpr std::array<FHookOperationSpec, 5> HookOperations = {{
    {"ModifyInputs", "Pre"},
    {"Cancel", "Pre"},
    {"Wrap", "On"},
    {"Replace", "On"},
    {"ModifyResult", "Post"},
}};

/// Checks a Hook stage name against the shared contract.
inline constexpr bool IsHookStage(std::string_view Name)
{
	for (const std::string_view Stage : HookStages)
	{
		if (Stage == Name)
		{
			return true;
		}
	}
	return false;
}

/// Finds a Hook operation in the shared contract.
inline constexpr const FHookOperationSpec* FindHookOperation(std::string_view Name)
{
	for (const FHookOperationSpec& Operation : HookOperations)
	{
		if (Name == Operation.Name)
		{
			return &Operation;
		}
	}
	return nullptr;
}

/// Literal shapes supported by the declaration DSL.
enum class EFieldType
{
	/// An arbitrary string literal.
	Text,
	/// A nonempty string literal.
	NonEmptyText,
	/// A two- or three-component numeric version string.
	Version,
	/// One configured identifier or string.
	Choice,
	/// A parenthesized list of configured identifiers or strings.
	ChoiceList,
	/// A qualified C++ struct name.
	StructType,
	/// A parenthesized list of quoted C++ class names.
	ClassNameList,
	/// A parenthesized list of quoted function names.
	FunctionNameList,
};

/// A declaration field and its choice source.
struct FFieldSpec
{
	/// Field name in the declaration.
	const char* Name;
	/// Required literal shape.
	EFieldType Type;
	/// Snapshot choice key, or null for free text.
	const char* ChoiceKey;
	/// Whether the field must be explicitly supplied.
	bool bRequired;
	/// Default scalar or single list entry, or null.
	const char* Default = nullptr;
};

/// Fields accepted by all three declarations.
inline constexpr std::array<FFieldSpec, 3> CommonFields = {{
    {"ExportVersion", EFieldType::Version, nullptr, false, DefaultExportVersion},
    {"DisplayName", EFieldType::NonEmptyText, nullptr, false},
    {"Description", EFieldType::Text, nullptr, false},
}};
/// Fields belonging to API declarations.
inline constexpr std::array<FFieldSpec, 6> APIFields = {{
    {"Scope", EFieldType::Choice, "APIScopes", true},
    {"Thread", EFieldType::Choice, "APIThreads", true},
    {"FailureRule", EFieldType::Choice, "APIFailureRules", true},
    {"Permissions", EFieldType::ChoiceList, "APIPermissions", false},
    {"FullView", EFieldType::ClassNameList, nullptr, false},
    {"CompatibleNames", EFieldType::FunctionNameList, nullptr, false},
}};
/// Fields belonging to Event declarations.
inline constexpr std::array<FFieldSpec, 7> EventFields = {{
    {"SourceScope", EFieldType::Choice, "EventSourceScopes", true},
    {"PublishRule", EFieldType::Choice, "EventPublishRules", true},
    {"PayloadType", EFieldType::StructType, nullptr, false},
    {"ExportId", EFieldType::NonEmptyText, nullptr, false},
    {"Permission", EFieldType::NonEmptyText, nullptr, false},
    {"PublishPermission", EFieldType::NonEmptyText, nullptr, false},
    {"Authority", EFieldType::Choice, "ExtensionAuthorities", false, "Server"},
}};
/// Fields belonging to Hook declarations.
inline constexpr std::array<FFieldSpec, 6> HookFields = {{
    {"ContextType", EFieldType::StructType, nullptr, false},
    {"ExportId", EFieldType::NonEmptyText, nullptr, false},
    {"Permission", EFieldType::NonEmptyText, nullptr, false},
    {"Authority", EFieldType::Choice, "ExtensionAuthorities", false, "Server"},
    {"AllowedStages", EFieldType::ChoiceList, "HookStages", false, DefaultHookStage},
    {"AllowedOperations", EFieldType::ChoiceList, "HookOperations", false},
}};

/// Returns the fields belonging to a declaration kind.
inline constexpr std::span<const FFieldSpec> FieldsFor(std::string_view Macro)
{
	if (Macro == "MOD_EXPORT_API")
	{
		return APIFields;
	}
	if (Macro == "MOD_EXPORT_EVENT")
	{
		return EventFields;
	}
	if (Macro == "MOD_EXPORT_HOOK")
	{
		return HookFields;
	}
	return {};
}

/// Finds a field in the common and declaration-specific schemas.
inline constexpr const FFieldSpec* FindField(std::string_view Macro, std::string_view Name)
{
	for (const FFieldSpec& Field : CommonFields)
	{
		if (Name == Field.Name)
		{
			return &Field;
		}
	}
	for (const FFieldSpec& Field : FieldsFor(Macro))
	{
		if (Name == Field.Name)
		{
			return &Field;
		}
	}
	return nullptr;
}

/// Computes an FNV-1a fingerprint to detect changed snapshot inputs.
inline std::uint64_t Fingerprint(std::string_view Bytes)
{
	std::uint64_t Hash = UINT64_C(14695981039346656037);
	for (unsigned char Byte : Bytes)
	{
		Hash ^= Byte;
		Hash *= UINT64_C(1099511628211);
	}
	return Hash;
}
} // namespace Schema
} // namespace Mod::ExportMetadata
