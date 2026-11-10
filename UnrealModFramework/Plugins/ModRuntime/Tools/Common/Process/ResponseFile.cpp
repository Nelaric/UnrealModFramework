// Copyright (c) 2026 Nelaric Contributors
#include "ResponseFile.h"

#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "Common/Process/ProcessArguments.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

void ResponseFile::WriteResponseFile(const fs::path& Path, const std::vector<std::string>& Arguments,
                                     std::string_view Format)
{
	std::string Text;
	for (const std::string& Argument : Arguments)
	{
		ProcessArguments::RequireArgument(Argument);
		if (Format == "msvc")
		{
			// MSVC requires an option and its separate value on the same line.
			Text += ProcessArguments::QuoteMsvc(Argument) + " ";
		}
		else if (Format == "gnu")
		{
			Text.push_back('"');
			for (char Character : Argument)
			{
				if (Character == '\\' || Character == '"')
				{
					Text.push_back('\\');
				}
				Text.push_back(Character);
			}
			Text += "\"\n";
		}
		else
		{
			throw std::runtime_error("Unknown response format: " + std::string(Format));
		}
	}
	if (Format == "msvc")
	{
		Text += "\r\n";
#ifdef _WIN32
		const std::wstring Wide = Path::PathFromUtf8(Text).wstring();
		std::string Encoded = "\xff\xfe";
		for (wchar_t Character : Wide)
		{
			Encoded.push_back(static_cast<char>(Character & 255));
			Encoded.push_back(static_cast<char>((Character >> 8) & 255));
		}
		File::WriteFileAtomically(Path, Encoded);
#else
		throw std::runtime_error("MSVC response files require a Windows host.");
#endif
	}
	else
	{
		File::WriteFileAtomically(Path, Text);
	}
}
} // namespace Mod::BuildTool
