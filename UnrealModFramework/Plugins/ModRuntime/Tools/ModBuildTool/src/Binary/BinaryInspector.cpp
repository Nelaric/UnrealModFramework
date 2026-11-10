// Copyright (c) 2026 Nelaric Contributors
#include "BinaryInspector.h"

#include "Common/Files/Path.h"
#include "ModBuildTool/src/Binary/ImageReader.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

void BinaryInspector::ValidateHostBinary(const fs::path& Path, std::string_view Platform, std::string_view Architecture,
                                         std::string_view Kind, std::string_view LinkWitness)
{
	if ((Platform != "Win64" && Platform != "Linux" && Platform != "Mac") ||
	    (Architecture != "x64" && Architecture != "arm64") || (Kind != "Executable" && Kind != "DynamicLibrary"))
	{
		throw std::runtime_error("Unsupported linked binary target or kind.");
	}
	if (!fs::is_regular_file(Path))
	{
		throw std::runtime_error("Linked binary is missing: " + Path::PathToUtf8(Path));
	}
	std::ifstream Stream(Path, std::ios::binary);
	if (!Stream)
	{
		throw std::runtime_error("Cannot open linked binary: " + Path::PathToUtf8(Path));
	}
	if (Platform == "Win64")
	{
		const std::string Dos = ImageReader::ReadHeader(Stream, 0, 64);
		const auto PeOffset = ImageReader::Integer(Dos, 60, 4);
		const std::string Header = ImageReader::ReadHeader(Stream, PeOffset, 26);
		const bool bDll = (ImageReader::Integer(Header, 22, 2) & 0x2000) != 0;
		if (!Dos.starts_with("MZ") || Header.substr(0, 4) != std::string("PE\0\0", 4) ||
		    ImageReader::Integer(Header, 4, 2) != (Architecture == "x64" ? 0x8664u : 0xaa64u) ||
		    ImageReader::Integer(Header, 20, 2) < 2 || ImageReader::Integer(Header, 24, 2) != 0x20b ||
		    bDll != (Kind == "DynamicLibrary") || (ImageReader::Integer(Header, 22, 2) & 0x0002) == 0)
		{
			throw std::runtime_error("Linked PE image kind or architecture mismatch.");
		}
		if (!LinkWitness.empty())
		{
			ImageReader::RequirePeExport(Stream, PeOffset, Header, LinkWitness);
		}
	}
	else if (Platform == "Linux")
	{
		const std::string Header = ImageReader::ReadHeader(Stream, 0, 64);
		const std::uint32_t Type = ImageReader::Integer(Header, 16, 2);
		if (Header.substr(0, 4) != std::string("\x7f"
		                                       "ELF",
		                                       4) ||
		    Header[4] != 2 || Header[5] != 1 ||
		    ImageReader::Integer(Header, 18, 2) != (Architecture == "x64" ? 62u : 183u) || (Type != 2 && Type != 3) ||
		    Header[6] != 1 || ImageReader::Integer(Header, 20, 4) != 1)
		{
			throw std::runtime_error("Linked ELF image kind or architecture mismatch.");
		}
		if (ImageReader::IsElfExecutable(Stream, Header, fs::file_size(Path)) != (Kind == "Executable"))
		{
			throw std::runtime_error("Linked ELF image is not the requested executable/shared-library kind.");
		}
	}
	else
	{
		const std::string Header = ImageReader::ReadHeader(Stream, 0, 32);
		const std::uint32_t Type = ImageReader::Integer(Header, 12, 4);
		if (ImageReader::Integer(Header, 0, 4) != 0xfeedfacf ||
		    ImageReader::Integer(Header, 4, 4) != (Architecture == "x64" ? 0x01000007u : 0x0100000cu) ||
		    (Kind == "Executable" ? Type != 2 : Type != 6 && Type != 8))
		{
			throw std::runtime_error("Linked Mach-O image kind or architecture mismatch; fat images are unsupported.");
		}
	}
}
} // namespace Mod::BuildTool
