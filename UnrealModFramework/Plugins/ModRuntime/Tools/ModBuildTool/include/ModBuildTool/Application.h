// Copyright (c) 2026 Nelaric Contributors
/** @file Application.h */
#pragma once

namespace Mod::BuildTool
{
/// Runs MBT with native narrow command-line arguments.
int RunApplication(int Count, char* Arguments[]);

#ifdef _WIN32
/// Runs MBT without losing native Windows Unicode arguments.
int RunApplication(int Count, wchar_t* Arguments[]);
#endif
} // namespace Mod::BuildTool
