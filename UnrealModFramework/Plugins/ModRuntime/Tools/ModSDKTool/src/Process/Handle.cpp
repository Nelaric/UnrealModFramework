// Copyright (c) 2026 Nelaric Contributors
#include "Handle.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Mod::SdkTool
{
#ifdef _WIN32
Handle::~Handle()
{
	if (Value != nullptr && Value != INVALID_HANDLE_VALUE)
	{
		CloseHandle(Value);
	}
}
#endif
} // namespace Mod::SdkTool
