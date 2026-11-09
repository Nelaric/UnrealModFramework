// Copyright (c) 2026 Nelaric Contributors
/** @file Application.h */
#pragma once

namespace Mod::HeaderTool
{
/// Runs the command-line application with native narrow arguments.
int RunApplication(int Count, char* Arguments[]);

#ifdef _WIN32
/// Runs the Windows application with native UTF-16 arguments.
int RunApplication(int Count, wchar_t* Arguments[]);
#endif
} // namespace Mod::HeaderTool
