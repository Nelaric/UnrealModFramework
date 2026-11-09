// Copyright (c) 2026 Nelaric Contributors
#include "Archive.h"

#include <charconv>
#include <cstdint>
#include <set>
#include <stdexcept>

namespace Mod::BuildTool
{
namespace
{
std::uint64_t ReadInteger(std::string_view Bytes, std::size_t Offset, std::size_t Width, bool bBigEndian)
{
	if (Width > 8 || Offset > Bytes.size() || Width > Bytes.size() - Offset)
	{
		throw std::runtime_error("Truncated archive or object integer.");
	}
	std::uint64_t Result = 0;
	for (std::size_t Index = 0; Index < Width; ++Index)
	{
		const std::size_t Position = bBigEndian ? Index : Width - 1 - Index;
		Result = (Result << 8) | static_cast<unsigned char>(Bytes[Offset + Position]);
	}
	return Result;
}

std::size_t ParseSize(std::string_view Text)
{
	while (!Text.empty() && Text.back() == ' ')
	{
		Text.remove_suffix(1);
	}
	std::size_t Value = 0;
	const auto Result = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
	if (Result.ec != std::errc() || Result.ptr != Text.data() + Text.size())
	{
		throw std::runtime_error("Invalid archive member size.");
	}
	return Value;
}

void ReadSymbols(std::string_view Strings, std::size_t Count, std::set<std::string>& Symbols)
{
	std::size_t Offset = 0;
	for (std::size_t Index = 0; Index < Count; ++Index)
	{
		const std::size_t End = Strings.find('\0', Offset);
		if (End == std::string_view::npos)
		{
			throw std::runtime_error("Truncated archive symbol strings.");
		}
		Symbols.emplace(Strings.substr(Offset, End - Offset));
		Offset = End + 1;
	}
}

void ReadUnixIndex(std::string_view Data, std::size_t Width, std::set<std::string>& Symbols)
{
	const std::uint64_t Count = ReadInteger(Data, 0, Width, true);
	if (Count > (Data.size() - Width) / Width)
	{
		throw std::runtime_error("Invalid archive symbol index.");
	}
	ReadSymbols(Data.substr(Width + static_cast<std::size_t>(Count) * Width), static_cast<std::size_t>(Count), Symbols);
}

void ReadBsdIndex(std::string_view Data, bool bWide, std::set<std::string>& Symbols)
{
	const std::size_t Width = bWide ? 8 : 4;
	const std::uint64_t EntryBytes = ReadInteger(Data, 0, Width, false);
	if (Data.size() < Width * 2 || EntryBytes > Data.size() - Width * 2 || EntryBytes % (Width * 2) != 0)
	{
		throw std::runtime_error("Invalid BSD archive symbol index.");
	}
	const std::size_t End = Width + static_cast<std::size_t>(EntryBytes);
	const std::uint64_t StringBytes = ReadInteger(Data, End, Width, false);
	if (StringBytes > Data.size() - End - Width)
	{
		throw std::runtime_error("Truncated BSD archive string table.");
	}
	const std::string_view Strings = Data.substr(End + Width, static_cast<std::size_t>(StringBytes));
	for (std::size_t Offset = Width; Offset < End; Offset += Width * 2)
	{
		const std::uint64_t NameOffset = ReadInteger(Data, Offset, Width, false);
		if (NameOffset >= Strings.size())
		{
			throw std::runtime_error("Invalid BSD archive symbol offset.");
		}
		const std::size_t Terminator = Strings.find('\0', static_cast<std::size_t>(NameOffset));
		if (Terminator == std::string_view::npos)
		{
			throw std::runtime_error("Unterminated BSD archive symbol.");
		}
		Symbols.emplace(Strings.substr(static_cast<std::size_t>(NameOffset), Terminator - NameOffset));
	}
}

void CheckObject(std::string_view Data, std::string_view Platform, std::string_view Architecture)
{
	if (Platform == "Win64")
	{
		std::uint64_t Machine = ReadInteger(Data, 0, 2, false);
		if (Machine == 0 && ReadInteger(Data, 2, 2, false) == 0xffff)
		{
			if (ReadInteger(Data, 4, 2, false) < 2)
			{
				throw std::runtime_error("A host archive contains an import-library member.");
			}
			Machine = ReadInteger(Data, 6, 2, false);
		}
		const std::uint64_t Expected = Architecture == "x64" ? 0x8664 : 0xaa64;
		if (Machine != Expected)
		{
			throw std::runtime_error("Host archive COFF architecture mismatch or unsupported LTO object.");
		}
	}
	else if (Platform == "Linux")
	{
		if (Data.size() < 20 ||
		    Data.substr(0, 4) != std::string_view("\x7f"
		                                          "ELF",
		                                          4) ||
		    Data[4] != 2 || Data[5] != 1 || ReadInteger(Data, 16, 2, false) != 1 ||
		    ReadInteger(Data, 18, 2, false) != (Architecture == "x64" ? 62 : 183))
		{
			throw std::runtime_error("Host archive ELF architecture mismatch or unsupported object format.");
		}
	}
	else
	{
		if (ReadInteger(Data, 0, 4, false) != 0xfeedfacf ||
		    ReadInteger(Data, 4, 4, false) != (Architecture == "x64" ? 0x01000007 : 0x0100000c) ||
		    ReadInteger(Data, 12, 4, false) != 1)
		{
			throw std::runtime_error("Host archive Mach-O architecture mismatch or unsupported object format.");
		}
	}
}
} // namespace

void ValidateHostArchive(const fs::path& Path, std::string_view Platform, std::string_view Architecture,
                         const std::vector<std::string>& RequiredSymbols)
{
	if ((Platform != "Win64" && Platform != "Linux" && Platform != "Mac") ||
	    (Architecture != "x64" && Architecture != "arm64"))
	{
		throw std::runtime_error("Unsupported native archive target.");
	}
	const std::string Storage = ReadFile(Path);
	const std::string_view Bytes = Storage;
	if (!Bytes.starts_with("!<arch>\n"))
	{
		throw std::runtime_error("Expected a regular static archive: " + PathToUtf8(Path));
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
		const std::size_t Size = ParseSize(Bytes.substr(Offset + 48, 10));
		Offset += 60;
		if (Size > Bytes.size() - Offset)
		{
			throw std::runtime_error("Truncated archive member.");
		}
		std::string_view Data = Bytes.substr(Offset, Size);
		if (Name.starts_with("#1/"))
		{
			const std::size_t NameSize = ParseSize(Name.substr(3));
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
				ReadUnixIndex(Data, Name == "/" ? 4 : 8, Symbols);
				bSeenFirstIndex = true;
			}
		}
		else if (Name.starts_with("__.SYMDEF"))
		{
			ReadBsdIndex(Data, Name.find("_64") != std::string_view::npos, Symbols);
		}
		else if (Name != "//")
		{
			CheckObject(Data, Platform, Architecture);
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
