// Copyright (c) 2026 Nelaric Contributors
/** @file GenerationFields.h */
#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class GenerationFields
{
public:
	static std::string Required(const Json& Object, const char* Key);
	static void RequireName(const std::string& Text);
	static std::string Fold(std::string Text);
	static fs::path Resolve(const fs::path& Root, const std::string& Text);

private:
	static bool Identifier(std::string_view Text);
};
} // namespace Mod::HeaderTool
