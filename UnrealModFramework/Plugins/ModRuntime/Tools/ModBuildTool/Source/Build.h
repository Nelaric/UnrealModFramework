// Copyright (c) 2026 Nelaric Contributors
/** @file Build.h */
#pragma once

#include "FileSystem.h"

#include <nlohmann/json.hpp>

namespace Mod::BuildTool
{
using FJson = nlohmann::json;

/// Hashes the parsed, sorted JSON context used by generated manifests.
std::string HashHostContext(const fs::path& Context);
/// Validates a context and complete generation batch without building.
FJson PlanHostBuild(const fs::path& Context, const fs::path& Manifest);
/// Builds or reuses one owning module's independently compiled host archive.
FJson BuildHost(const fs::path& Context, const fs::path& Manifest, std::string_view ExpectedInput);
/// Associates a current host build with a trusted successful link record.
FJson FinalizeHost(const fs::path& Context, const fs::path& Manifest, const fs::path& LinkReceipt);
/// Invalidates UE's UHT dependencies when a generation watch changes.
void RefreshUeWatches(const fs::path& ContextSet);
/// Verifies and records the owning images after UE 5.6's link actions.
FJson FinalizeUeTarget(const fs::path& ContextSet, const fs::path& TargetReceipt);
/// Validates a result destination before any build-side effects.
void ValidateResultOutput(const fs::path& Output, const fs::path& Context, const fs::path& Manifest,
                          const fs::path& LinkReceipt = {});
/// Refuses JSON output that would replace a declared build input or artifact.
void WriteResult(const FJson& Result, const fs::path& Output, const fs::path& Context, const fs::path& Manifest,
                 const fs::path& LinkReceipt = {});
} // namespace Mod::BuildTool
