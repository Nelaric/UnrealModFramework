// Copyright (c) 2026 Nelaric Contributors
/** @file ModExportMacros.h
 * @brief Source markers for Mod API, event, and hook declarations.
 *
 * @details The
 * export tools read API and Event metadata and generate
 * Hook dispatch from the marked method. The markers add no
 * runtime code.
 * Use explicit Unreal reflection macros when reflection is required.
 */
#pragma once

/** @brief Marks a function as a Mod API declaration.
 *
 * @param ... Metadata tokens reserved for the export generator.
 */
#define MOD_EXPORT_API(...)

/** @brief Marks an event source or signature as a Mod event declaration.
 *
 * @param ... Metadata tokens reserved for the export generator.
 */
#define MOD_EXPORT_EVENT(...)

namespace Mod::ExportMetadata
{
/// Returns the value and marks a business error for source analysis.
template <typename T> constexpr T SourceFailure(const char*, unsigned int, T Value) noexcept
{
	return Value;
}
} // namespace Mod::ExportMetadata

/** @brief Marks a business failure with its native return value.
 * @param Name Stable, uppercase MOD_ERROR_* name published in the SDK.
 * @param Code Fixed numeric code in the Business domain.
 * @param Value Native failure value returned from the API.
 */
#define MOD_SOURCE_FAILURE(Name, Code, Value) ::Mod::ExportMetadata::SourceFailure(#Name, Code, Value)

namespace Mod::ExportMetadata::Private
{
// The preprocessor folds whitespace in stringified macro arguments to single spaces.
constexpr bool ConsumeHookToken(const char*& Text, const char* Token)
{
	const char* Cursor = Text;
	while (*Token)
	{
		if (*Cursor++ != *Token++)
		{
			return false;
		}
	}
	Text = *Cursor == ' ' ? Cursor + 1 : Cursor;
	return true;
}

// Bootstrap builds check field spelling and duplication. MHT validates names and IDs.
constexpr bool HasValidHookFields(const char* Text)
{
	unsigned Seen = 0;
	while (*Text)
	{
		unsigned Field = 0;
		if (ConsumeHookToken(Text, "CompatibleNames"))
		{
			Field = 1;
			if (!ConsumeHookToken(Text, "=") || !ConsumeHookToken(Text, "("))
			{
				return false;
			}
			int Depth = 1;
			while (*Text && Depth)
			{
				Depth += (*Text == '(') - (*Text == ')');
				++Text;
			}
			if (Depth)
			{
				return false;
			}
			if (*Text == ' ')
			{
				++Text;
			}
		}
		else
		{
			constexpr const char* Flags[]{"NoPre", "NoOn", "NoPost"};
			unsigned Bit = 2;
			for (const char* Flag : Flags)
			{
				if (ConsumeHookToken(Text, Flag))
				{
					Field = Bit;
					break;
				}
				Bit <<= 1;
			}
		}
		if (!Field || (Seen & Field))
		{
			return false;
		}
		Seen |= Field;
		if (*Text && (!ConsumeHookToken(Text, ",") || !*Text))
		{
			return false;
		}
	}
	return true;
}
} // namespace Mod::ExportMetadata::Private

/** @brief Marks a method for generated Pre, On and Post dispatch.
 *
 * @param ... NoPre, NoOn, NoPost, or a
 * CompatibleNames list.
 */
#define MOD_EXPORT_HOOK(...)                                                                                           \
	static_assert(::Mod::ExportMetadata::Private::HasValidHookFields("" #__VA_ARGS__),                                 \
	              "MOD_EXPORT_HOOK accepts NoPre, NoOn, NoPost and CompatibleNames = (...) once each");
