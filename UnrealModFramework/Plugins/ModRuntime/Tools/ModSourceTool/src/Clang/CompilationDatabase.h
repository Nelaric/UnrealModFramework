// Copyright (c) 2026 Nelaric Contributors
/** @file CompilationDatabase.h */
#pragma once

#include "ModSourceTool/src/Analysis/SourceRequest.h"
#include "ModSourceTool/src/Clang/Clang.h"
#include "ModSourceTool/src/Clang/CompileCommand.h"

namespace Mod::SourceTool
{
class CompilationDatabase
{
public:
	static CompileCommand LoadCompileCommand(const Clang& Clang, const SourceRequest& Request);
};
} // namespace Mod::SourceTool
