// Copyright (c) 2026 Nelaric Contributors
/** @file TranslationUnitParser.h */
#pragma once

#include "ModSourceTool/src/Clang/Clang.h"
#include "ModSourceTool/src/Clang/CompileCommand.h"

#include <clang-c/Index.h>
#include <filesystem>
#include <memory>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;
using TranslationUnit = std::unique_ptr<CXTranslationUnitImpl, decltype(&::clang_disposeTranslationUnit)>;

class TranslationUnitParser
{
public:
	static TranslationUnit ParseUnit(const Clang& Clang, CXIndex Index, const CompileCommand& Command,
	                                 const fs::path& Source);
};
} // namespace Mod::SourceTool
