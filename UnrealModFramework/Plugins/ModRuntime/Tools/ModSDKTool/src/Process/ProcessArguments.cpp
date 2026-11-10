// Copyright (c) 2026 Nelaric Contributors
#include "ProcessArguments.h"

#include "Common/Files/Path.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace Mod::SdkTool
{
#ifdef _WIN32
std::wstring ProcessArguments::Quote(std::string_view Value)
{
	const std::wstring Wide = Mod::BuildTool::Path::PathFromUtf8(Value).wstring();
	std::wstring Result = L"\"";
	std::size_t Slashes = 0;
	for (wchar_t Character : Wide)
	{
		if (Character == L'\\')
		{
			++Slashes;
			continue;
		}
		Result.append(Slashes * (Character == L'"' ? 2 : 1), L'\\');
		Slashes = 0;
		if (Character == L'"')
		{
			Result.push_back(L'\\');
		}
		Result.push_back(Character);
	}
	Result.append(Slashes * 2, L'\\');
	return Result + L'"';
}
#endif
} // namespace Mod::SdkTool
