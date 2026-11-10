// Copyright (c) 2026 Nelaric Contributors
#include "ProcessEnvironment.h"

#include <cstddef>
#include <cwctype>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

#ifndef _WIN32
extern char** environ;
#endif

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

std::map<std::string, std::string> ProcessEnvironment::CaptureEnvironment()
{
	std::map<std::string, std::string> Result;
#ifdef _WIN32
	LPWCH Block = GetEnvironmentStringsW();
	if (Block == nullptr)
	{
		throw std::runtime_error("Cannot read the process environment.");
	}
	for (const wchar_t* Entry = Block; *Entry != L'\0';)
	{
		const std::wstring Item(Entry);
		const std::size_t Equals = Item.find(L'=', 1);
		if (Equals != std::wstring::npos)
		{
			std::wstring Name = Item.substr(0, Equals);
			for (wchar_t& Character : Name)
			{
				Character = static_cast<wchar_t>(std::towupper(Character));
			}
			const std::u8string Utf8Name = fs::path(Name).u8string();
			const std::u8string Utf8Value = fs::path(Item.substr(Equals + 1)).u8string();
			Result[std::string(Utf8Name.begin(), Utf8Name.end())] = std::string(Utf8Value.begin(), Utf8Value.end());
		}
		Entry += Item.size() + 1;
	}
	FreeEnvironmentStringsW(Block);
#else
	for (char** Entry = environ; Entry != nullptr && *Entry != nullptr; ++Entry)
	{
		const std::string Item(*Entry);
		const std::size_t Equals = Item.find('=');
		if (Equals != std::string::npos)
		{
			Result[Item.substr(0, Equals)] = Item.substr(Equals + 1);
		}
	}
#endif
	return Result;
}
} // namespace Mod::BuildTool
