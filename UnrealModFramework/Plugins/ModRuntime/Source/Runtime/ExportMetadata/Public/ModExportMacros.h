// Copyright (c) 2026 Nelaric Contributors
/** @file ModExportMacros.h
 * @brief Source markers for Mod API, event, and hook declarations.
 *
 * @details Markers
 * accept metadata tokens and expand to nothing in C++.
 * Metadata parsing and code generation are reserved for the
 * export tools.
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

/** @brief Marks a gameplay function as a Mod hook declaration.
 *
 * @param ... Metadata tokens reserved for the export
 * generator.
 */
#define MOD_EXPORT_HOOK(...)
