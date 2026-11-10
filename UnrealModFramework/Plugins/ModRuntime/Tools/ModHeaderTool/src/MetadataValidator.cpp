// Copyright (c) 2026 Nelaric Contributors
#include "ModHeaderTool/MetadataValidator.h"

#include "ModExportMetadataSchema.h"
#include "SourceLexer.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string_view>
#include <utility>

namespace Mod::HeaderTool
{
namespace
{
namespace Schema = Mod::ExportMetadata::Schema;

bool IsVersion(std::string_view Text)
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

bool IsTypeName(std::string_view Text)
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

class FValidator
{
public:
	FValidator(FExportMacro& InMacro, const FMetadataOptions* InOptions, const std::string& Source)
	    : Macro(InMacro), Options(InOptions), Locations(Macro.File, Source)
	{
	}

	std::vector<std::string> Validate()
	{
		std::set<std::string> Names;
		for (const FMetadataField& Field : Macro.Fields)
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
					FMetadataField Field;
					Field.Name = Spec.Name;
					Field.Offset = Macro.Offset;
					Field.Value.Offset = Macro.Offset;
					if (Spec.Type == Schema::EFieldType::ChoiceList)
					{
						Field.Value.Kind = EMetadataValueKind::List;
						if (Spec.Default)
						{
							FMetadataValue Value;
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
		return Diagnostics;
	}

private:
	void Error(std::size_t Offset, const std::string& Reason)
	{
		const auto [Line, Column] = Locations.Location(Offset);
		Diagnostics.push_back(PathToUtf8(Macro.File) + "(" + std::to_string(Line) + "," + std::to_string(Column) +
		                      "): error: " + Macro.Name + ": " + Reason);
	}

	void CheckChoice(const Schema::FFieldSpec& Spec, const FMetadataValue& Value)
	{
		if (Value.Kind != EMetadataValueKind::String && Value.Kind != EMetadataValueKind::Identifier)
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
			const std::string Reason = Spec.Type == Schema::EFieldType::ExportId
			                               ? "is not a registered GameplayTag"
			                               : std::string("is not an allowed option in '") + Spec.ChoiceKey + "'";
			Error(Value.Offset, std::string("'") + Spec.Name + "' value '" + Value.Text + "' " + Reason + ".");
		}
	}

	void ValidateValue(const Schema::FFieldSpec& Spec, const FMetadataValue& Value)
	{
		using Schema::EFieldType;
		if (Spec.Type == EFieldType::ChoiceList || Spec.Type == EFieldType::ClassNameList ||
		    Spec.Type == EFieldType::FunctionNameList)
		{
			if (Value.Kind != EMetadataValueKind::List)
			{
				Error(Value.Offset, std::string("'") + Spec.Name + "' expects a parenthesized list.");
				return;
			}
			if (Spec.ChoiceKey && std::string_view(Spec.ChoiceKey) == "HookStages" && Value.Items.empty())
			{
				Error(Value.Offset, "'AllowedStages' must contain at least one Hook stage.");
			}
			std::set<std::string> Items;
			for (const FMetadataValue& Item : Value.Items)
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
					if (Item.Kind != EMetadataValueKind::String || !IsCompatibleFunctionName(Item.Text))
					{
						Error(Item.Offset,
						      "'CompatibleNames' expects a quoted function identifier, found '" + Item.Text + "'.");
					}
				}
				else if (Item.Kind != EMetadataValueKind::String || !IsTypeName(Item.Text))
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
			if ((Value.Kind != EMetadataValueKind::String && Value.Kind != EMetadataValueKind::Identifier) ||
			    !IsTypeName(Value.Text))
			{
				Error(Value.Offset, std::string("'") + Spec.Name + "' expects a C++ struct type name.");
			}
			return;
		}
		if (Value.Kind != EMetadataValueKind::String)
		{
			Error(Value.Offset, std::string("'") + Spec.Name + "' expects a string literal.");
			return;
		}
		if ((Spec.Type == EFieldType::NonEmptyText || Spec.Type == EFieldType::ExportId) &&
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
		else if (Spec.Type == EFieldType::ExportId)
		{
			CheckChoice(Spec, Value);
		}
	}

	FExportMacro& Macro;
	const FMetadataOptions* Options;
	Detail::FLexer Locations;
	std::vector<std::string> Diagnostics;
};
} // namespace

bool IsCompatibleFunctionName(std::string_view Name)
{
	const auto IsLetter = [](char Character)
	{ return (Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z'); };
	if (Name.empty() || (!IsLetter(Name.front()) && Name.front() != '_') ||
	    !std::all_of(Name.begin(), Name.end(), [&](char Character)
	                 { return IsLetter(Character) || (Character >= '0' && Character <= '9') || Character == '_'; }))
	{
		return false;
	}
	constexpr std::string_view Keywords[] = {
	    "alignas",     "alignof",  "and",       "and_eq",    "asm",       "auto",         "bitand",
	    "bitor",       "bool",     "break",     "case",      "catch",     "char",         "char8_t",
	    "char16_t",    "char32_t", "class",     "co_await",  "co_return", "co_yield",     "compl",
	    "concept",     "const",    "consteval", "constexpr", "constinit", "const_cast",   "continue",
	    "decltype",    "default",  "delete",    "do",        "double",    "dynamic_cast", "else",
	    "enum",        "explicit", "export",    "extern",    "false",     "float",        "for",
	    "friend",      "goto",     "if",        "inline",    "int",       "long",         "mutable",
	    "namespace",   "new",      "noexcept",  "not",       "not_eq",    "nullptr",      "operator",
	    "or",          "or_eq",    "private",   "protected", "public",    "register",     "reinterpret_cast",
	    "requires",    "return",   "short",     "signed",    "sizeof",    "static",       "static_assert",
	    "static_cast", "struct",   "switch",    "template",  "this",      "thread_local", "throw",
	    "true",        "try",      "typedef",   "typeid",    "typename",  "union",        "unsigned",
	    "using",       "virtual",  "void",      "volatile",  "wchar_t",   "while",        "xor",
	    "xor_eq"};
	return std::find(std::begin(Keywords), std::end(Keywords), Name) == std::end(Keywords);
}

std::vector<std::string> ValidateMetadata(FExportMacro& Macro, const FMetadataOptions* Options,
                                          const std::string& Source)
{
	return FValidator(Macro, Options, Source).Validate();
}
} // namespace Mod::HeaderTool
