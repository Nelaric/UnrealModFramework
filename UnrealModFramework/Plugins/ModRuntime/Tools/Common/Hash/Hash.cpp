// Copyright (c) 2026 Nelaric Contributors
#include "Hash.h"

#include "Common/Files/Path.h"
#include "Common/Hash/Sha256.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

std::string Hash::HashBytes(std::string_view Bytes)
{
	Sha256 Hash;
	Hash.Update(Bytes);
	return Hash.Finish();
}

std::string Hash::HashFile(const fs::path& Path)
{
	if (!fs::is_regular_file(Path))
	{
		throw std::runtime_error("Cannot hash a missing or non-regular file: " + Path::PathToUtf8(Path));
	}
	std::ifstream Stream(Path, std::ios::binary);
	if (!Stream)
	{
		throw std::runtime_error("Cannot open hash input: " + Path::PathToUtf8(Path));
	}
	Sha256 Hash;
	std::array<char, 65536> Buffer{};
	while (Stream)
	{
		Stream.read(Buffer.data(), static_cast<std::streamsize>(Buffer.size()));
		Hash.Update(std::string_view(Buffer.data(), static_cast<std::size_t>(Stream.gcount())));
	}
	if (!Stream.eof())
	{
		throw std::runtime_error("Cannot read hash input: " + Path::PathToUtf8(Path));
	}
	return Hash.Finish();
}

void Hash::RequireDigest(std::string_view Digest, std::string_view Field)
{
	if (Digest.size() != 64 || Digest.find_first_not_of("0123456789abcdef") != std::string_view::npos)
	{
		throw std::runtime_error(std::string(Field) + " must be a lowercase SHA-256 digest.");
	}
}
} // namespace Mod::BuildTool
