// Copyright (c) 2026 Nelaric Contributors
#include "ImageReader.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Mod::BuildTool
{
std::string ImageReader::ReadHeader(std::ifstream& Stream, std::uint64_t Offset, std::size_t Size)
{
	if (Offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
	{
		throw std::runtime_error("Binary header offset is out of range.");
	}
	Stream.seekg(static_cast<std::streamoff>(Offset));
	std::string Bytes(Size, '\0');
	Stream.read(Bytes.data(), static_cast<std::streamsize>(Size));
	if (Stream.gcount() != static_cast<std::streamsize>(Size))
	{
		throw std::runtime_error("Truncated linked binary header.");
	}
	return Bytes;
}

std::uint32_t ImageReader::Integer(std::string_view Bytes, std::size_t Offset, std::size_t Width)
{
	if (Width > 4 || Offset > Bytes.size() || Width > Bytes.size() - Offset)
	{
		throw std::runtime_error("Truncated linked binary field.");
	}
	std::uint32_t Value = 0;
	for (std::size_t Index = 0; Index < Width; ++Index)
	{
		Value |= std::uint32_t(static_cast<unsigned char>(Bytes[Offset + Index])) << (Index * 8);
	}
	return Value;
}

std::uint64_t ImageReader::WideInteger(std::string_view Bytes, std::size_t Offset)
{
	return std::uint64_t(Integer(Bytes, Offset, 4)) | (std::uint64_t(Integer(Bytes, Offset + 4, 4)) << 32);
}

void ImageReader::RequirePeExport(std::ifstream& Stream, std::uint64_t PeOffset, std::string_view Header,
                                  std::string_view Name)
{
	const auto OptionalSize = Integer(Header, 20, 2);
	const auto SectionCount = Integer(Header, 6, 2);
	if (OptionalSize < 120 || SectionCount == 0 || SectionCount > 96)
	{
		throw std::runtime_error("Invalid PE export or section table.");
	}
	const auto Optional = ReadHeader(Stream, PeOffset + 24, 120);
	const auto ExportRva = Integer(Optional, 112, 4);
	const auto ExportSize = Integer(Optional, 116, 4);
	if (Integer(Optional, 108, 4) == 0 || ExportRva == 0 || ExportSize < 40)
	{
		throw std::runtime_error("The linked PE image has no export directory.");
	}
	const auto Sections = ReadHeader(Stream, PeOffset + 24 + OptionalSize, SectionCount * 40);
	const auto ReadRva = [&](std::uint64_t Rva, std::size_t Size, bool bBounded = false)
	{
		for (std::size_t Index = 0; Index < Sections.size(); Index += 40)
		{
			const auto Base = Integer(Sections, Index + 12, 4);
			const auto RawSize = Integer(Sections, Index + 16, 4);
			if (Rva >= Base && Rva - Base < RawSize)
			{
				const auto Available = static_cast<std::size_t>(RawSize - (Rva - Base));
				if (bBounded || Size <= Available)
				{
					return ReadHeader(Stream, Integer(Sections, Index + 20, 4) + (Rva - Base),
					                  Size <= Available ? Size : Available);
				}
			}
		}
		throw std::runtime_error("PE export RVA is outside the image's file data.");
	};
	const auto Exports = ReadRva(ExportRva, 40);
	const auto Functions = Integer(Exports, 28, 4);
	const auto Names = Integer(Exports, 32, 4);
	const auto Ordinals = Integer(Exports, 36, 4);
	std::uint64_t First = 0;
	std::uint64_t Last = Integer(Exports, 24, 4);
	while (First < Last)
	{
		const auto Index = First + (Last - First) / 2;
		const auto NameRva = Integer(ReadRva(Names + Index * 4, 4), 0, 4);
		const auto Bytes = ReadRva(NameRva, Name.size() + 1, true);
		const auto End = Bytes.find('\0');
		const auto Candidate = std::string_view(Bytes).substr(0, End);
		if (Candidate == Name && End != std::string::npos)
		{
			const auto Ordinal = Integer(ReadRva(Ordinals + Index * 2, 2), 0, 2);
			if (Ordinal < Integer(Exports, 20, 4))
			{
				const auto Address = Integer(ReadRva(Functions + std::uint64_t(Ordinal) * 4, 4), 0, 4);
				if (Address != 0 && (Address < ExportRva || Address - ExportRva >= ExportSize))
				{
					ReadRva(Address, 1);
					return;
				}
			}
			break;
		}
		if (Candidate < Name)
		{
			First = Index + 1;
		}
		else
		{
			Last = Index;
		}
	}
	throw std::runtime_error("The linked PE image does not retain the expected link witness: " + std::string(Name));
}

bool ImageReader::IsElfExecutable(std::ifstream& Stream, std::string_view Header, std::uint64_t FileSize)
{
	const std::uint64_t Table = WideInteger(Header, 32);
	const std::uint32_t EntrySize = Integer(Header, 54, 2);
	const std::uint32_t Count = Integer(Header, 56, 2);
	if (EntrySize < 56 || Count == 0 || Count == 0xffff || Table > FileSize || Count > (FileSize - Table) / EntrySize)
	{
		throw std::runtime_error("Invalid or unsupported ELF program header table.");
	}
	bool bExecutable = Integer(Header, 16, 2) == 2;
	for (std::uint32_t Index = 0; Index < Count; ++Index)
	{
		const std::string Segment = ReadHeader(Stream, Table + std::uint64_t(Index) * EntrySize, 56);
		const std::uint32_t Type = Integer(Segment, 0, 4);
		if (Type == 3)
		{
			bExecutable = true;
		}
		else if (Type == 2)
		{
			const std::uint64_t Offset = WideInteger(Segment, 8);
			const std::uint64_t Size = WideInteger(Segment, 32);
			if (Offset > FileSize || Size > FileSize - Offset || Size % 16 != 0 || Size / 16 > 65536)
			{
				throw std::runtime_error("Invalid or unsupported ELF dynamic table.");
			}
			for (std::uint64_t Position = 0; Position < Size; Position += 16)
			{
				const std::string Entry = ReadHeader(Stream, Offset + Position, 16);
				const std::uint64_t Tag = WideInteger(Entry, 0);
				if (Tag == 0)
				{
					break;
				}
				if (Tag == 0x6ffffffb && (WideInteger(Entry, 8) & 0x08000000) != 0)
				{
					bExecutable = true;
				}
			}
		}
	}
	return bExecutable;
}

namespace
{
void RequireRange(std::uint64_t Offset, std::uint64_t Length, std::uint64_t FileSize)
{
	if (Offset > FileSize || Length > FileSize - Offset)
	{
		throw std::runtime_error("Linked symbol table exceeds the image bounds.");
	}
}

bool HasName(std::string_view Strings, std::size_t Offset, std::string_view Name)
{
	return Offset < Strings.size() && Name.size() < Strings.size() - Offset &&
	       Strings.substr(Offset, Name.size()) == Name && Strings[Offset + Name.size()] == '\0';
}
} // namespace

void ImageReader::RequireElfSymbol(std::ifstream& Stream, std::string_view Header, std::uint64_t Size,
                                   std::string_view Name)
{
	const auto Offset = WideInteger(Header, 40);
	const auto Stride = Integer(Header, 58, 2);
	const auto Count = Integer(Header, 60, 2);
	if (Stride < 64 || Count == 0)
	{
		throw std::runtime_error("Missing ELF section headers.");
	}
	RequireRange(Offset, std::uint64_t(Count) * Stride, Size);
	for (unsigned Index = 0; Index < Count; ++Index)
	{
		const auto Section = ReadHeader(Stream, Offset + std::uint64_t(Index) * Stride, 64);
		const auto Type = Integer(Section, 4, 4);
		if (Type != 11 && Type != 2) // SHT_DYNSYM or SHT_SYMTAB
		{
			continue;
		}
		const auto Link = Integer(Section, 40, 4);
		const auto Bytes = WideInteger(Section, 32);
		if (Link >= Count || WideInteger(Section, 56) != 24 || Bytes % 24 != 0)
		{
			throw std::runtime_error("Invalid ELF symbol table.");
		}
		const auto Names = ReadHeader(Stream, Offset + std::uint64_t(Link) * Stride, 64);
		const auto NamesSize = WideInteger(Names, 32);
		const auto SymbolsOffset = WideInteger(Section, 24);
		const auto NamesOffset = WideInteger(Names, 24);
		RequireRange(SymbolsOffset, Bytes, Size);
		RequireRange(NamesOffset, NamesSize, Size);
		const std::string Symbols = ReadHeader(Stream, SymbolsOffset, Bytes);
		const std::string Strings = ReadHeader(Stream, NamesOffset, NamesSize);
		for (std::size_t Pos = 0; Pos < Symbols.size(); Pos += 24)
		{
			const auto Info = Integer(Symbols, Pos + 4, 1);
			if ((Info >> 4) != 0 && (Info & 15) == 2 && Integer(Symbols, Pos + 6, 2) != 0 &&
			    HasName(Strings, Integer(Symbols, Pos, 4), Name))
			{
				return;
			}
		}
	}
	throw std::runtime_error("Missing defined ELF host binding: " + std::string(Name));
}

void ImageReader::RequireMachSymbol(std::ifstream& Stream, std::string_view Header, std::uint64_t Size,
                                    std::string_view Name)
{
	const std::string Decorated = "_" + std::string(Name);
	std::uint64_t Offset = 32;
	const auto CommandsEnd = Offset + Integer(Header, 20, 4);
	RequireRange(Offset, CommandsEnd - Offset, Size);
	for (unsigned Index = 0; Index < Integer(Header, 16, 4); ++Index)
	{
		RequireRange(Offset, 8, CommandsEnd);
		const auto Command = ReadHeader(Stream, Offset, 8);
		const auto Length = Integer(Command, 4, 4);
		if (Length < 8)
		{
			throw std::runtime_error("Invalid Mach-O load command.");
		}
		RequireRange(Offset, Length, CommandsEnd);
		if (Integer(Command, 0, 4) == 2 && Length >= 24) // LC_SYMTAB
		{
			const auto Table = ReadHeader(Stream, Offset, 24);
			const auto Bytes = std::uint64_t(Integer(Table, 12, 4)) * 16;
			const auto StringSize = Integer(Table, 20, 4);
			RequireRange(Integer(Table, 8, 4), Bytes, Size);
			RequireRange(Integer(Table, 16, 4), StringSize, Size);
			const auto Symbols = ReadHeader(Stream, Integer(Table, 8, 4), Bytes);
			const auto Strings = ReadHeader(Stream, Integer(Table, 16, 4), StringSize);
			for (std::size_t Pos = 0; Pos < Symbols.size(); Pos += 16)
			{
				// N_EXT | N_SECT is a defined external symbol, excluding debugger entries.
				if ((Integer(Symbols, Pos + 4, 1) & 0xef) == 0x0f && Integer(Symbols, Pos + 5, 1) != 0 &&
				    HasName(Strings, Integer(Symbols, Pos, 4), Decorated))
				{
					return;
				}
			}
		}
		Offset += Length;
	}
	throw std::runtime_error("Missing defined Mach-O host binding: " + std::string(Name));
}
} // namespace Mod::BuildTool
