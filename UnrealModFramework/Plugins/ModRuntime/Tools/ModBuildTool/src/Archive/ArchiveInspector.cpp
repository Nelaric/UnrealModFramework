// Copyright (c) 2026 Nelaric Contributors
#include "ArchiveInspector.h"

#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "ModBuildTool/src/Archive/ArchiveReader.h"

#include <cstddef>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

void ArchiveInspector::ValidateHostArchive(const fs::path& Path, std::string_view Platform,
                                           std::string_view Architecture,
                                           const std::vector<std::string>& RequiredSymbols)
{
	if ((Platform != "Win64" && Platform != "Linux" && Platform != "Mac") ||
	    (Architecture != "x64" && Architecture != "arm64"))
	{
		throw std::runtime_error("Unsupported native archive target.");
	}
	const std::string Storage = File::ReadFile(Path);
	const std::string_view Bytes = Storage;
	if (!Bytes.starts_with("!<arch>\n"))
	{
		throw std::runtime_error("Expected a regular static archive: " + Path::PathToUtf8(Path));
	}
	if (RequiredSymbols.empty())
	{
		throw std::runtime_error("Archive validation requires at least one symbol.");
	}
	std::set<std::string> Symbols;
	bool bHasObject = false;
	bool bSeenFirstIndex = false;
	std::size_t Offset = 8;
	while (Offset < Bytes.size())
	{
		if (Bytes.size() - Offset < 60 || Bytes.substr(Offset + 58, 2) != "`\n")
		{
			throw std::runtime_error("Invalid archive member header.");
		}
		std::string_view Name = Bytes.substr(Offset, 16);
		while (!Name.empty() && Name.back() == ' ')
		{
			Name.remove_suffix(1);
		}
		const std::size_t Size = ArchiveReader::ParseSize(Bytes.substr(Offset + 48, 10));
		Offset += 60;
		if (Size > Bytes.size() - Offset)
		{
			throw std::runtime_error("Truncated archive member.");
		}
		std::string_view Data = Bytes.substr(Offset, Size);
		if (Name.starts_with("#1/"))
		{
			const std::size_t NameSize = ArchiveReader::ParseSize(Name.substr(3));
			if (NameSize > Data.size())
			{
				throw std::runtime_error("Truncated BSD member name.");
			}
			Name = Data.substr(0, NameSize);
			while (!Name.empty() && Name.back() == '\0')
			{
				Name.remove_suffix(1);
			}
			Data.remove_prefix(NameSize);
		}
		if (Name == "/" || Name == "/SYM64/")
		{
			if (!bSeenFirstIndex)
			{
				ArchiveReader::ReadUnixIndex(Data, Name == "/" ? 4 : 8, Symbols);
				bSeenFirstIndex = true;
			}
		}
		else if (Name.starts_with("__.SYMDEF"))
		{
			ArchiveReader::ReadBsdIndex(Data, Name.find("_64") != std::string_view::npos, Symbols);
		}
		else if (Name != "//")
		{
			ArchiveReader::CheckObject(Data, Platform, Architecture);
			bHasObject = true;
		}
		Offset += Size;
		if (Size % 2 != 0)
		{
			if (Offset == Bytes.size())
			{
				throw std::runtime_error("Missing archive member padding.");
			}
			++Offset;
		}
	}
	if (!bHasObject)
	{
		throw std::runtime_error("Host archive has no native objects.");
	}
	std::string Missing;
	for (const std::string& Symbol : RequiredSymbols)
	{
		if (Symbol.empty() || Symbol.find('\0') != std::string::npos)
		{
			throw std::runtime_error("Required symbols must be nonempty and contain no NUL.");
		}
		const std::string Decorated = Platform == "Mac" ? "_" + Symbol : Symbol;
		if (!Symbols.contains(Decorated))
		{
			Missing += (Missing.empty() ? "" : ", ") + Symbol;
		}
	}
	if (!Missing.empty())
	{
		throw std::runtime_error("Host archive lacks required symbols: " + Missing);
	}
}
} // namespace Mod::BuildTool
