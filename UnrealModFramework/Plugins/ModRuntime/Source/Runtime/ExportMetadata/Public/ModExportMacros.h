// Copyright (c) 2026 Nelaric Contributors
/** @file ModExportMacros.h
 * @brief Source markers for Mod API, event, and hook declarations.
 *
 * @details The export tools read API and Event metadata and generate
 * Hook dispatch from the marked method. The markers add no runtime code.
 * Use explicit Unreal reflection macros when reflection is required.
 */
#pragma once

/** @brief Marks a function as a Mod API declaration.
 *
 * @param ... Metadata tokens reserved for the export
 * generator.
 */
#define MOD_EXPORT_API(...)

/** @brief Marks an event source or signature as a Mod event declaration.
 *
 * @param ... Metadata tokens reserved for
 * the export generator.
 */
#define MOD_EXPORT_EVENT(...)

namespace Mod::ExportMetadata::Private
{
// Stringification trims outer whitespace and folds each inner whitespace run to one space.
// Check the field even in bootstrap builds. MHT validates the names and their generated IDs.
constexpr bool HasOnlyHookNames(const char* Text)
{
	if (!*Text)
	{
		return true;
	}
	for (const char* Key = "CompatibleNames"; *Key; ++Key)
	{
		if (*Text++ != *Key)
		{
			return false;
		}
	}
	if (*Text == ' ')
	{
		++Text;
	}
	if (*Text++ != '=')
	{
		return false;
	}
	if (*Text == ' ')
	{
		++Text;
	}
	if (*Text++ != '(')
	{
		return false;
	}
	int Depth = 1;
	while (*Text && Depth)
	{
		Depth += (*Text == '(') - (*Text == ')');
		++Text;
	}
	return Depth == 0 && !*Text;
}
} // namespace Mod::ExportMetadata::Private

/** @brief Marks a method for generated Pre, On and Post dispatch.
 *
 * @param ... Optional CompatibleNames list of former function names.
 */
#define MOD_EXPORT_HOOK(...)                                                                                           \
	static_assert(::Mod::ExportMetadata::Private::HasOnlyHookNames("" #__VA_ARGS__),                                   \
	              "MOD_EXPORT_HOOK accepts only CompatibleNames = (...)");
