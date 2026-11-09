// Copyright (c) 2026 Nelaric Contributors
/** @file Generation.h */
#pragma once

#include "ModHeaderTool/FileSystem.h"

namespace Mod::HeaderTool
{
/// Resolves active C++ APIs and publishes complete generated artifacts.
void Generate(const fs::path& BuildInput, const fs::path& OutputDirectory, const fs::path& Options,
              const fs::path& ClangLibrary, const fs::path& HostContexts, const fs::path& LifecycleSource = {});
} // namespace Mod::HeaderTool
