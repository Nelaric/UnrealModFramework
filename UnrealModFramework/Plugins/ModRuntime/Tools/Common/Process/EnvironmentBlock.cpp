// Copyright (c) 2026 Nelaric Contributors
#include "EnvironmentBlock.h"

#include "Common/Files/Path.h"
#include "Common/Process/EnvironmentKeyLess.h"

#include <map>
#include <string>
#include <vector>

namespace Mod::BuildTool
{
#ifdef _WIN32
std::vector<wchar_t> EnvironmentBlock::MakeEnvironment(const std::map<std::string, std::string>& Overrides)
{
	std::map<std::wstring, std::wstring, EnvironmentKeyLess> Values;
	for (const auto& [Name, Value] : Overrides)
	{
		Values[Path::PathFromUtf8(Name).wstring()] = Path::PathFromUtf8(Value).wstring();
	}
	std::vector<wchar_t> Result;
	if (Values.empty())
	{
		Result.push_back(L'\0');
	}
	for (const auto& [Name, Value] : Values)
	{
		const std::wstring Item = Name + L"=" + Value;
		Result.insert(Result.end(), Item.begin(), Item.end());
		Result.push_back(L'\0');
	}
	Result.push_back(L'\0');
	return Result;
}
#endif
} // namespace Mod::BuildTool
