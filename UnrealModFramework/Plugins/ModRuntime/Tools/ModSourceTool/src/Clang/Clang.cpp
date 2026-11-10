// Copyright (c) 2026 Nelaric Contributors
#include "Clang.h"

#include <clang-c/Index.h>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace Mod::SourceTool
{
Clang::Clang(const std::filesystem::path& Path)
#ifdef _WIN32
    : Library(LoadLibraryW(Path.c_str()))
#else
    : Library(dlopen(Path.c_str(), RTLD_NOW | RTLD_LOCAL))
#endif
{
}

std::string Clang::String(CXString Value) const
{
	const char* Text = clang_getCString(Value);
	const std::string Result = Text ? Text : "";
	clang_disposeString(Value);
	return Result;
}
} // namespace Mod::SourceTool
