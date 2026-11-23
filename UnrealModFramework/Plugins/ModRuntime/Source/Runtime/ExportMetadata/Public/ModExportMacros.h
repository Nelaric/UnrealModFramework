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

#if defined(_MSVC_TRADITIONAL) && _MSVC_TRADITIONAL
// The traditional MSVC preprocessor only warns about extra arguments to a nullary macro.
#define MOD_EXPORT_HOOK(...) static_assert(sizeof("" #__VA_ARGS__) == 1, "MOD_EXPORT_HOOK() takes no arguments");
#else
/// Marks a gameplay method for generated Pre, On and Post dispatch.
#define MOD_EXPORT_HOOK()
#endif
