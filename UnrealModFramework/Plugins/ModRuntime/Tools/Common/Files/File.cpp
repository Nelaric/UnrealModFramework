// Copyright (c) 2026 Nelaric Contributors
#include "File.h"

#include "Common/Files/Path.h"
#include "Common/Files/TemporaryName.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <Windows.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#endif

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

std::string File::ReadFile(const fs::path& Path)
{
	const fs::path NativePath = Path::FileSystemPath(Path);
	if (!fs::is_regular_file(NativePath))
	{
		throw std::runtime_error("Not a regular file: " + Path::PathToUtf8(Path));
	}
	std::ifstream Stream(NativePath, std::ios::binary);
	if (!Stream)
	{
		throw std::runtime_error("Cannot open file: " + Path::PathToUtf8(Path));
	}
	std::string Contents((std::istreambuf_iterator<char>(Stream)), std::istreambuf_iterator<char>());
	if (Stream.bad())
	{
		throw std::runtime_error("Cannot read file: " + Path::PathToUtf8(Path));
	}
	return Contents;
}

void File::ReplaceFileAtomically(const fs::path& Source, const fs::path& Destination)
{
#ifdef _WIN32
	const fs::path NativeSource = Path::FileSystemPath(Source);
	const fs::path NativeDestination = Path::FileSystemPath(Destination);
	if (!MoveFileExW(NativeSource.c_str(), NativeDestination.c_str(),
	                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		const auto Error = GetLastError();
		throw std::runtime_error("Cannot publish file: " + Path::PathToUtf8(Destination) + " (Windows error " +
		                         std::to_string(Error) + ").");
	}
#else
	fs::rename(Source, Destination);
#endif
}

void File::WriteFileAtomically(const fs::path& Path, std::string_view Contents)
{
	fs::create_directories(Path::FileSystemPath(Path.parent_path()));
	fs::path Temporary = Path;
	Temporary += ".tmp-" + TemporaryName::UniqueSuffix();
	const fs::path NativeTemporary = Path::FileSystemPath(Temporary);
	try
	{
		std::ofstream Stream(NativeTemporary, std::ios::binary | std::ios::trunc);
		Stream.write(Contents.data(), static_cast<std::streamsize>(Contents.size()));
		Stream.close();
		if (!Stream)
		{
			throw std::runtime_error("Cannot write file: " + Path::PathToUtf8(Temporary));
		}
		ReplaceFileAtomically(Temporary, Path);
	}
	catch (...)
	{
		std::error_code Ignored;
		fs::remove(NativeTemporary, Ignored);
		throw;
	}
}
} // namespace Mod::BuildTool
