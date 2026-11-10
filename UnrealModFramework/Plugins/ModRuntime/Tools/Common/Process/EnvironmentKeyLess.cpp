// Copyright (c) 2026 Nelaric Contributors
#include "EnvironmentKeyLess.h"

#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Mod::BuildTool
{
#ifdef _WIN32
bool EnvironmentKeyLess::operator()(const std::wstring& Left, const std::wstring& Right) const
{
	return _wcsicmp(Left.c_str(), Right.c_str()) < 0;
}
#endif
} // namespace Mod::BuildTool
