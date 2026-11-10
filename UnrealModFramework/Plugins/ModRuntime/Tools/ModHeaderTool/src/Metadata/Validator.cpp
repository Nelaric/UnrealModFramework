// Copyright (c) 2026 Nelaric Contributors
#include "Validator.h"

#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Metadata/MetadataField.h"
#include "ModHeaderTool/src/Metadata/MetadataName.h"

#include "ModExportMetadataSchema.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Mod::HeaderTool
{
namespace Schema = Mod::ExportMetadata::Schema;

Validator::Validator(ExportMacro& InMacro, const MetadataOptions* InOptions, const std::string& Source)
    : Macro(InMacro), Options(InOptions), Locations(Macro.File, Source)
{
}

std::vector<std::string> Validator::Validate()
{
	std::set<std::string> Names;
	for (const MetadataField& Field : Macro.Fields)
	{
		if (!Names.insert(Field.Name).second)
		{
			Error(Field.Offset, "duplicate field '" + Field.Name + "'.");
			continue;
		}
		const Schema::FFieldSpec* Spec = Schema::FindField(Macro.Name, Field.Name);
		if (!Spec)
		{
			Error(Field.Offset, "unknown field '" + Field.Name + "'.");
			continue;
		}
		ValidateValue(*Spec, Field.Value);
	}
	const auto CompleteFields = [this, &Names](std::span<const Schema::FFieldSpec> Fields)
	{
		for (const Schema::FFieldSpec& Spec : Fields)
		{
			if (Names.contains(Spec.Name))
			{
				continue;
			}
			if (Spec.bRequired)
			{
				Error(Macro.Offset, std::string("missing required field '") + Spec.Name + "'.");
			}
			else if (Spec.Default || Spec.Type == Schema::EFieldType::ChoiceList)
			{
				MetadataField Field;
				Field.Name = Spec.Name;
				Field.Offset = Macro.Offset;
				Field.Value.Offset = Macro.Offset;
				if (Spec.Type == Schema::EFieldType::ChoiceList)
				{
					Field.Value.Kind = MetadataValue::EKind::List;
					if (Spec.Default)
					{
						MetadataValue Value;
						Value.Text = Spec.Default;
						Value.Offset = Macro.Offset;
						Field.Value.Items.push_back(std::move(Value));
					}
				}
				else
				{
					Field.Value.Text = Spec.Default;
				}
				ValidateValue(Spec, Field.Value);
				Macro.Fields.push_back(std::move(Field));
			}
		}
	};
	CompleteFields(Schema::CommonFields);
	CompleteFields(Schema::FieldsFor(Macro.Name));
	if (Macro.Name == "MOD_EXPORT_HOOK" && Diagnostics.empty())
	{
		ValidateHookPolicy();
	}
	return Diagnostics;
}

bool Validator::IsVersion(std::string_view Text)
{
	std::size_t Begin = 0;
	int Parts = 0;
	while (Begin < Text.size())
	{
		const std::size_t End = Text.find('.', Begin);
		const std::string_view Part = Text.substr(Begin, End == Text.npos ? Text.size() - Begin : End - Begin);
		if (Part.empty() || (Part.size() > 1 && Part.front() == '0') ||
		    !std::all_of(Part.begin(), Part.end(), [](char Character) { return Character >= '0' && Character <= '9'; }))
		{
			return false;
		}
		++Parts;
		if (End == Text.npos)
		{
			return Parts == 2 || Parts == 3;
		}
		Begin = End + 1;
	}
	return false;
}

bool Validator::IsTypeName(std::string_view Text)
{
	std::size_t Index = Text.starts_with("::") ? 2 : 0;
	while (Index < Text.size())
	{
		const unsigned char First = static_cast<unsigned char>(Text[Index]);
		if (!std::isalpha(First) && First != '_' && First < 0x80)
		{
			return false;
		}
		++Index;
		while (Index < Text.size() && (std::isalnum(static_cast<unsigned char>(Text[Index])) || Text[Index] == '_' ||
		                               static_cast<unsigned char>(Text[Index]) >= 0x80))
		{
			++Index;
		}
		if (Index == Text.size())
		{
			return true;
		}
		if (Text.substr(Index, 2) != "::")
		{
			return false;
		}
		Index += 2;
	}
	return false;
}

void Validator::Error(std::size_t Offset, const std::string& Reason)
{
	const auto [Line, Column] = Locations.Location(Offset);
	Diagnostics.push_back(DiagnosticLocation::FormatDiagnosticLocation(Macro.File, Line, Column) +
	                      ": error: " + Macro.Name + ": " + Reason);
}

void Validator::ValidateHookPolicy()
{
	const auto& Stages = std::ranges::find(Macro.Fields, "AllowedStages", &MetadataField::Name)->Value.Items;
	const auto& Operations = std::ranges::find(Macro.Fields, "AllowedOperations", &MetadataField::Name)->Value.Items;
	for (const MetadataValue& Stage : Stages)
	{
		if (!Schema::IsHookStage(Stage.Text))
		{
			Error(Stage.Offset, "unknown Hook stage '" + Stage.Text + "'.");
		}
	}
	for (const MetadataValue& Operation : Operations)
	{
		const Schema::FHookOperationSpec* Spec = Schema::FindHookOperation(Operation.Text);
		if (!Spec)
		{
			Error(Operation.Offset, "unknown Hook operation '" + Operation.Text + "'.");
			continue;
		}
		if (std::ranges::find(Stages, Spec->Stage, &MetadataValue::Text) == Stages.end())
		{
			Error(Operation.Offset, "Hook operation '" + Operation.Text + "' requires stage '" + Spec->Stage + "'.");
		}
	}
}

void Validator::CheckChoice(const Schema::FFieldSpec& Spec, const MetadataValue& Value)
{
	if (Value.Kind != MetadataValue::EKind::String && Value.Kind != MetadataValue::EKind::Identifier)
	{
		Error(Value.Offset, std::string("'") + Spec.Name + "' expects a configured identifier or string.");
		return;
	}
	if (!Options)
	{
		return;
	}
	const auto Choices = Options->Choices.find(Spec.ChoiceKey);
	if (Choices == Options->Choices.end() ||
	    std::find(Choices->second.begin(), Choices->second.end(), Value.Text) == Choices->second.end())
	{
		Error(Value.Offset, std::string("'") + Spec.Name + "' value '" + Value.Text +
		                        "' is not an allowed option in '" + Spec.ChoiceKey + "'.");
	}
}

void Validator::ValidateValue(const Schema::FFieldSpec& Spec, const MetadataValue& Value)
{
	using Schema::EFieldType;
	if (Spec.Type == EFieldType::ChoiceList || Spec.Type == EFieldType::ClassNameList ||
	    Spec.Type == EFieldType::FunctionNameList)
	{
		if (Value.Kind != MetadataValue::EKind::List)
		{
			Error(Value.Offset, std::string("'") + Spec.Name + "' expects a parenthesized list.");
			return;
		}
		if (Spec.ChoiceKey && std::string_view(Spec.ChoiceKey) == "HookStages" && Value.Items.empty())
		{
			Error(Value.Offset, "'AllowedStages' must contain at least one Hook stage.");
		}
		std::set<std::string> Items;
		for (const MetadataValue& Item : Value.Items)
		{
			std::string Name = Item.Text;
			if (Spec.Type == EFieldType::ClassNameList && Name.starts_with("::"))
			{
				Name.erase(0, 2);
			}
			if (!Items.insert(Name).second)
			{
				Error(Item.Offset, std::string("duplicate '") + Spec.Name + "' entry '" + Item.Text + "'.");
			}
			if (Spec.Type == EFieldType::ChoiceList)
			{
				CheckChoice(Spec, Item);
			}
			else if (Spec.Type == EFieldType::FunctionNameList)
			{
				if (Item.Kind != MetadataValue::EKind::String || !MetadataName::IsCompatibleFunctionName(Item.Text))
				{
					Error(Item.Offset,
					      "'CompatibleNames' expects a quoted function identifier, found '" + Item.Text + "'.");
				}
			}
			else if (Item.Kind != MetadataValue::EKind::String || !IsTypeName(Item.Text))
			{
				Error(Item.Offset, std::string("'") + Spec.Name + "' expects quoted C++ class names.");
			}
		}
		return;
	}
	if (Spec.Type == EFieldType::Choice)
	{
		CheckChoice(Spec, Value);
		return;
	}
	if (Spec.Type == EFieldType::StructType)
	{
		if ((Value.Kind != MetadataValue::EKind::String && Value.Kind != MetadataValue::EKind::Identifier) ||
		    !IsTypeName(Value.Text))
		{
			Error(Value.Offset, std::string("'") + Spec.Name + "' expects a C++ struct type name.");
		}
		return;
	}
	if (Value.Kind != MetadataValue::EKind::String)
	{
		Error(Value.Offset, std::string("'") + Spec.Name + "' expects a string literal.");
		return;
	}
	if (Spec.Type == EFieldType::NonEmptyText &&
	    (Value.Text.empty() || std::all_of(Value.Text.begin(), Value.Text.end(),
	                                       [](unsigned char Character) { return std::isspace(Character) != 0; })))
	{
		Error(Value.Offset, std::string("'") + Spec.Name + "' must not be empty.");
		return;
	}
	if (Spec.Type == EFieldType::Version && !IsVersion(Value.Text))
	{
		Error(Value.Offset, "'ExportVersion' must match 'major.minor' or 'major.minor.patch'.");
	}
}
} // namespace Mod::HeaderTool
