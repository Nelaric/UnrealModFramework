// Copyright (c) 2026 Nelaric Contributors
#include "JsonDocument.h"

#include "Common/Files/File.h"
#include "Common/Files/Path.h"

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

Json JsonDocument::ReadJson(const fs::path& Path)
{
	if (!fs::is_regular_file(Path) || fs::file_size(Path) > 16 * 1024 * 1024)
	{
		throw std::runtime_error("Missing or oversized JSON input: " + Mod::BuildTool::Path::PathToUtf8(Path));
	}
	try
	{
		return Json::parse(Mod::BuildTool::File::ReadFile(Path));
	}
	catch (const Json::exception& Error)
	{
		throw std::runtime_error("Invalid JSON in " + Mod::BuildTool::Path::PathToUtf8(Path) + ": " + Error.what());
	}
}

std::string JsonDocument::Text(const Json& Document, const char* Key)
{
	const std::string Value = Document.at(Key).get<std::string>();
	if (Value.empty() || Value.size() > 32768 ||
	    std::any_of(Value.begin(), Value.end(), [](unsigned char Character) { return Character < 32; }))
	{
		throw std::runtime_error(std::string(Key) + " must be a bounded nonempty single-line string.");
	}
	return Value;
}

void JsonDocument::RequireFormat(const Json& Document, std::string_view Format, std::initializer_list<int> Versions)
{
	if (Format == "UMF.SdkContract" && Document.contains("projectionVersion") && Document.contains("exports"))
	{
		throw std::runtime_error(
		    "UnsupportedProjection: native UE view contracts are not ContextHandlesV1 client contracts. "
		    "A matching export producer must supply UMF.SdkContract and checked client endpoints.");
	}
	if (!Document.is_object() || Document.value("format", "") != Format || !Document.contains("schemaVersion") ||
	    std::none_of(Versions.begin(), Versions.end(),
	                 [&](int Version) { return Document.at("schemaVersion") == Version; }))
	{
		throw std::runtime_error("Unsupported format or schemaVersion for " + std::string(Format) + ".");
	}
}
} // namespace Mod::SdkTool
