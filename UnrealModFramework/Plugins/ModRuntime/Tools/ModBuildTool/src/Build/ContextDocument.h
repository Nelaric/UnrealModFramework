// Copyright (c) 2026 Nelaric Contributors
/** @file ContextDocument.h */
#pragma once

#include <filesystem>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class ContextDocument
{
public:
	static std::string Text(const Json& Document, std::string_view Key);
	static Json ReadJson(const fs::path& Path);
	static void RequireFormat(const Json& Document, std::string_view Format, std::initializer_list<int> Versions = {1});
	static void RequireSymbol(std::string_view Symbol);
	static void RequireChoice(const Json& Document, std::string_view Key,
	                          std::initializer_list<std::string_view> Choices);
};
} // namespace Mod::BuildTool
