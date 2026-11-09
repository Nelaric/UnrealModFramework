// Copyright (c) 2026 Nelaric Contributors
#include "Binary.h"

#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace Mod::BuildTool
{
namespace
{
std::string ReadHeader(std::ifstream& Stream, std::uint64_t Offset, std::size_t Size)
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

std::uint32_t Integer(std::string_view Bytes, std::size_t Offset, std::size_t Width)
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

std::uint64_t WideInteger(std::string_view Bytes, std::size_t Offset)
{
	return std::uint64_t(Integer(Bytes, Offset, 4)) | (std::uint64_t(Integer(Bytes, Offset + 4, 4)) << 32);
}

void RequirePeExport(std::ifstream& Stream, std::uint64_t PeOffset, std::string_view Header, std::string_view Name)
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
	throw std::runtime_error("The linked PE image does not retain the current archive's link witness: " +
	                         std::string(Name));
}

bool IsElfExecutable(std::ifstream& Stream, std::string_view Header, std::uint64_t FileSize)
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
} // namespace

void ValidateHostBinary(const fs::path& Path, std::string_view Platform, std::string_view Architecture,
                        std::string_view Kind, std::string_view LinkWitness)
{
	if ((Platform != "Win64" && Platform != "Linux" && Platform != "Mac") ||
	    (Architecture != "x64" && Architecture != "arm64") || (Kind != "Executable" && Kind != "DynamicLibrary"))
	{
		throw std::runtime_error("Unsupported linked binary target or kind.");
	}
	if (!fs::is_regular_file(Path))
	{
		throw std::runtime_error("Linked binary is missing: " + PathToUtf8(Path));
	}
	std::ifstream Stream(Path, std::ios::binary);
	if (!Stream)
	{
		throw std::runtime_error("Cannot open linked binary: " + PathToUtf8(Path));
	}
	if (Platform == "Win64")
	{
		const std::string Dos = ReadHeader(Stream, 0, 64);
		const auto PeOffset = Integer(Dos, 60, 4);
		const std::string Header = ReadHeader(Stream, PeOffset, 26);
		const bool bDll = (Integer(Header, 22, 2) & 0x2000) != 0;
		if (!Dos.starts_with("MZ") || Header.substr(0, 4) != std::string("PE\0\0", 4) ||
		    Integer(Header, 4, 2) != (Architecture == "x64" ? 0x8664u : 0xaa64u) || Integer(Header, 20, 2) < 2 ||
		    Integer(Header, 24, 2) != 0x20b || bDll != (Kind == "DynamicLibrary") ||
		    (Integer(Header, 22, 2) & 0x0002) == 0)
		{
			throw std::runtime_error("Linked PE image kind or architecture mismatch.");
		}
		if (!LinkWitness.empty())
		{
			RequirePeExport(Stream, PeOffset, Header, LinkWitness);
		}
	}
	else if (Platform == "Linux")
	{
		const std::string Header = ReadHeader(Stream, 0, 64);
		const std::uint32_t Type = Integer(Header, 16, 2);
		if (Header.substr(0, 4) != std::string("\x7f"
		                                       "ELF",
		                                       4) ||
		    Header[4] != 2 || Header[5] != 1 || Integer(Header, 18, 2) != (Architecture == "x64" ? 62u : 183u) ||
		    (Type != 2 && Type != 3) || Header[6] != 1 || Integer(Header, 20, 4) != 1)
		{
			throw std::runtime_error("Linked ELF image kind or architecture mismatch.");
		}
		if (IsElfExecutable(Stream, Header, fs::file_size(Path)) != (Kind == "Executable"))
		{
			throw std::runtime_error("Linked ELF image is not the requested executable/shared-library kind.");
		}
	}
	else
	{
		const std::string Header = ReadHeader(Stream, 0, 32);
		const std::uint32_t Type = Integer(Header, 12, 4);
		if (Integer(Header, 0, 4) != 0xfeedfacf ||
		    Integer(Header, 4, 4) != (Architecture == "x64" ? 0x01000007u : 0x0100000cu) ||
		    (Kind == "Executable" ? Type != 2 : Type != 6 && Type != 8))
		{
			throw std::runtime_error("Linked Mach-O image kind or architecture mismatch; fat images are unsupported.");
		}
	}
}
} // namespace Mod::BuildTool
