// Copyright (c) 2026 Nelaric Contributors
/** @file Handle.h */
#pragma once

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Mod::BuildTool
{
#ifdef _WIN32
struct Handle
{
	HANDLE Value = nullptr;
	~Handle();
};
#endif
} // namespace Mod::BuildTool
