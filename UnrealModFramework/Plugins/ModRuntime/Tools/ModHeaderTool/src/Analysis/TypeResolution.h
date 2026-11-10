// Copyright (c) 2026 Nelaric Contributors
/** @file TypeResolution.h */
#pragma once

#include "ModHeaderTool/src/Analysis/ClangLibrary.h"
#include "ModHeaderTool/src/Analysis/EngineHeader.h"
#include "ModHeaderTool/src/Generation/API.h"
#include "ModHeaderTool/src/Generation/APIType.h"
#include "ModHeaderTool/src/Generation/GenerationInput.h"
#include "ModHeaderTool/src/Generation/GenerationModel.h"
#include "ModHeaderTool/src/Generation/ModuleInput.h"
#include "ModHeaderTool/src/Generation/ResolvedType.h"
#include "ModHeaderTool/src/Scan/ExportMacro.h"

#include <clang-c/Index.h>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class TypeResolution
{
public:
	static const ModuleInput* OwnerModule(const GenerationInput& Input, const fs::path& File);
	static ResolvedType& ResolveRecord(GenerationModel& Model, ClangLibrary& Clang, CXCursor Record,
	                                   const ExportMacro& Macro, const std::string& ExportModule = {});
	static APIType ResolveType(GenerationModel& Model, ClangLibrary& Clang, CXType Type, const API& API, bool bInput);

private:
	static bool IsUObject(ClangLibrary& Clang, CXCursor Record, std::set<std::string>& Visited,
	                      std::string_view BaseName = "UObject");
	static EngineHeader FindEngineHeader(const GenerationInput& Input, const fs::path& File);
};
} // namespace Mod::HeaderTool
