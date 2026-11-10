// Copyright (c) 2026 Nelaric Contributors
#include "Generation.h"

#include "ModHeaderTool/src/Generation/APIGenerator.h"

#include <filesystem>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

void Generation::Generate(const fs::path& BuildInput, const fs::path& OutputDirectory, const fs::path& Options,
                          const fs::path& ClangLibrary, const fs::path& HostContexts, const fs::path& LifecycleSource)
{
	APIGenerator(BuildInput, HostContexts).Generate(OutputDirectory, Options, ClangLibrary, LifecycleSource);
}
} // namespace Mod::HeaderTool
