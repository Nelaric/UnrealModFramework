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
inline constexpr int SnapshotVersion = 1;
/// Default location relative to the project root.
inline constexpr const char* SnapshotPath = "Saved/ModHeaderTool/MetadataOptions.json";
/// Default declaration export version.
inline constexpr const char* DefaultExportVersion = "1.0.0";
/// Default C++ Hook stage.
inline constexpr const char* DefaultHookStage = "Observe";
/// Supported Hook stages in the metadata contract.
inline constexpr std::array<const char*, 3> HookStages = {"Observe", "Modify", "Intercept"};

/// Literal shapes supported by the declaration DSL.
enum class EFieldType
{
	/// An arbitrary string literal.
	Text,
	/// A nonempty string literal.
	NonEmptyText,
	/// A two- or three-component numeric version string.
	Version,
	/// A string literal naming a registered export ID.
	ExportId,
	/// One configured identifier or string.
	Choice,
	/// A parenthesized list of configured identifiers or strings.
	ChoiceList,
	/// A qualified C++ struct name.
	StructType,
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
inline constexpr std::array<FFieldSpec, 4> CommonFields = {{
    {"ExportId", EFieldType::ExportId, "ExportIds", true},
    {"ExportVersion", EFieldType::Version, nullptr, false, DefaultExportVersion},
    {"DisplayName", EFieldType::NonEmptyText, nullptr, false},
    {"Description", EFieldType::Text, nullptr, false},
}};
/// Fields belonging to API declarations.
inline constexpr std::array<FFieldSpec, 4> ApiFields = {{
    {"Scope", EFieldType::Choice, "ApiScopes", true},
    {"Thread", EFieldType::Choice, "ApiThreads", true},
    {"FailureRule", EFieldType::Choice, "ApiFailureRules", true},
    {"Permissions", EFieldType::ChoiceList, "ApiPermissions", false},
}};
/// Fields belonging to Event declarations.
inline constexpr std::array<FFieldSpec, 3> EventFields = {{
    {"SourceScope", EFieldType::Choice, "EventSourceScopes", true},
    {"PublishRule", EFieldType::Choice, "EventPublishRules", true},
    {"PayloadType", EFieldType::StructType, nullptr, false},
}};
/// Fields belonging to Hook declarations.
inline constexpr std::array<FFieldSpec, 2> HookFields = {{
    {"ContextType", EFieldType::StructType, nullptr, false},
    {"AllowedStages", EFieldType::ChoiceList, "HookStages", false, DefaultHookStage},
}};

/// Returns the fields belonging to a declaration kind.
inline constexpr std::span<const FFieldSpec> FieldsFor(std::string_view Macro)
{
	if (Macro == "MOD_EXPORT_API")
	{
		return ApiFields;
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
