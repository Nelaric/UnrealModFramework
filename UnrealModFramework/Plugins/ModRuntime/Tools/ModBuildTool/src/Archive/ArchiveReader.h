// Copyright (c) 2026 Nelaric Contributors
/** @file ArchiveReader.h */
#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>

namespace Mod::BuildTool
{
class ArchiveReader
{
public:
	static std::size_t ParseSize(std::string_view Text);
	static void ReadUnixIndex(std::string_view Data, std::size_t Width, std::set<std::string>& Symbols);
	static void ReadBsdIndex(std::string_view Data, bool bWide, std::set<std::string>& Symbols);
	static void CheckObject(std::string_view Data, std::string_view Platform, std::string_view Architecture);

private:
	static std::uint64_t ReadInteger(std::string_view Bytes, std::size_t Offset, std::size_t Width, bool bBigEndian);
	static void ReadSymbols(std::string_view Strings, std::size_t Count, std::set<std::string>& Symbols);
};
} // namespace Mod::BuildTool
