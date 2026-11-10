// Copyright (c) 2026 Nelaric Contributors
/** @file DeclarationBinding.h */
#pragma once

#include "ModHeaderTool/src/Analysis/ClangLibrary.h"
#include "ModHeaderTool/src/Analysis/Source.h"
#include "ModHeaderTool/src/Generation/API.h"
#include "ModHeaderTool/src/Generation/GenerationModel.h"
#include "ModHeaderTool/src/Scan/ExportMacro.h"

#include <clang-c/Index.h>
#include <cstddef>
#include <string>
#include <string_view>

namespace Mod::HeaderTool
{
class DeclarationBinding
{
public:
	static void BindSignature(GenerationModel& Model, ClangLibrary& Clang, CXCursor Method, API& API);
	static API Bind(GenerationModel& Model, ClangLibrary& Clang, ExportMacro Macro, const Source& Source,
	                CXCursor Method);

private:
	static std::string Field(const ExportMacro& Macro, std::string_view Name);
	static bool CheckGap(const ExportMacro& Macro, const std::string& Source, std::size_t DeclarationBegin);
};
} // namespace Mod::HeaderTool
