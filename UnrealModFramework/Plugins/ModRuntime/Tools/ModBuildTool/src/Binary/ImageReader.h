// Copyright (c) 2026 Nelaric Contributors
/** @file ImageReader.h */
#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

namespace Mod::BuildTool
{
class ImageReader
{
public:
	static std::string ReadHeader(std::ifstream& Stream, std::uint64_t Offset, std::size_t Size);
	static std::uint32_t Integer(std::string_view Bytes, std::size_t Offset, std::size_t Width);
	static void RequirePeExport(std::ifstream& Stream, std::uint64_t PeOffset, std::string_view Header,
	                            std::string_view Name);
	static bool IsElfExecutable(std::ifstream& Stream, std::string_view Header, std::uint64_t FileSize);

private:
	static std::uint64_t WideInteger(std::string_view Bytes, std::size_t Offset);
};
} // namespace Mod::BuildTool
