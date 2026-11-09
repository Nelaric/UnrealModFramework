// Copyright (c) 2026 Nelaric Contributors
/** @file Application.h */
#pragma once

namespace Mod::SdkTool
{
/// Runs the SDK command line; returns zero only for a successful operation.
int RunApplication(int Count, char* Arguments[]);

#ifdef _WIN32
/// Accepts native Windows arguments without losing Unicode file paths.
int RunApplication(int Count, wchar_t* Arguments[]);
#endif
} // namespace Mod::SdkTool
