// Copyright (c) 2026 Nelaric Contributors
/** @file JsonDocument.h */
#pragma once

#include <filesystem>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class JsonDocument
{
public:
	/// Reads bounded JSON with path-qualified diagnostics.
	static Json ReadJson(const fs::path& Path);
	/// Reads a required nonempty JSON string without control characters.
	static std::string Text(const Json& Document, const char* Key);
	/// Requires a protocol format and supported schema version.
	static void RequireFormat(const Json& Document, std::string_view Format, std::initializer_list<int> Versions = {1});
};
} // namespace Mod::SdkTool
