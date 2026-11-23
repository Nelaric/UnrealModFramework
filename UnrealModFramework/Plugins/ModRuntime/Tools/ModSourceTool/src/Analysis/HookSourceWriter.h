// Copyright (c) 2026 Nelaric Contributors
/** @file HookSourceWriter.h */
#pragma once

#include <filesystem>

namespace Mod::SourceTool
{
namespace HookSourceWriter
{
void Rewrite(const std::filesystem::path& RequestFile);
} // namespace HookSourceWriter
} // namespace Mod::SourceTool
