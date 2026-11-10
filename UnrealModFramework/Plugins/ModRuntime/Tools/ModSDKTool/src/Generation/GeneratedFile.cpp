// Copyright (c) 2026 Nelaric Contributors
#include "GeneratedFile.h"

#include "Common/Files/File.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

void GeneratedFile::WriteSource(const fs::path& File, std::string_view TextValue)
{
	std::string Encoded = "\xef\xbb\xbf";
	for (char Character : TextValue)
	{
		if (Character == '\n')
		{
			Encoded.push_back('\r');
		}
		Encoded.push_back(Character);
	}
	Mod::BuildTool::File::WriteFileAtomically(File, Encoded);
}
} // namespace Mod::SdkTool
