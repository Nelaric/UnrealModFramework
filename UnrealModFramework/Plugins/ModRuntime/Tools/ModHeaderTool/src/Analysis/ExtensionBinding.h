// Copyright (c) 2026 Nelaric Contributors
/** @file ExtensionBinding.h */
#pragma once

#include "ModHeaderTool/src/Analysis/ClangLibrary.h"
#include "ModHeaderTool/src/Analysis/Source.h"
#include "ModHeaderTool/src/Generation/Extension.h"

namespace Mod::HeaderTool
{
Extension BindExtension(GenerationModel& Model, ClangLibrary& Clang, const Source& Source, ExportMacro Macro,
                        CXCursor Member);
}
