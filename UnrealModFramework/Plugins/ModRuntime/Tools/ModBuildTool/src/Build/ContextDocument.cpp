// Copyright (c) 2026 Nelaric Contributors
#include "ContextDocument.h"

#include "Common/Files/File.h"
#include "Common/Files/Path.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

std::string ContextDocument::Text(const Json& Document, std::string_view Key)
{
	const std::string Value = Document.at(std::string(Key)).get<std::string>();
	if (Value.empty() || Value.find('\0') != std::string::npos)
	{
		throw std::runtime_error(std::string(Key) + " must be a nonempty string without NUL characters.");
	}
	return Value;
}

Json ContextDocument::ReadJson(const fs::path& Path)
{
	try
	{
		return Json::parse(File::ReadFile(Path));
	}
	catch (const Json::exception& Error)
	{
		throw std::runtime_error("Invalid JSON in " + Path::PathToUtf8(Path) + ": " + Error.what());
	}
}

void ContextDocument::RequireFormat(const Json& Document, std::string_view Format, std::initializer_list<int> Versions)
{
	if (!Document.is_object() || Document.value("format", "") != Format || !Document.contains("schemaVersion") ||
	    !Document["schemaVersion"].is_number_integer() ||
	    std::none_of(Versions.begin(), Versions.end(),
	                 [&](int Version) { return Document["schemaVersion"] == Version; }))
	{
		throw std::runtime_error("Unsupported format or schemaVersion for " + std::string(Format) + ".");
	}
}

void ContextDocument::RequireSymbol(std::string_view Symbol)
{
	if (Symbol.empty() || (!std::isalpha(static_cast<unsigned char>(Symbol.front())) && Symbol.front() != '_') ||
	    Symbol.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
	        std::string_view::npos)
	{
		throw std::runtime_error("Required symbols must be undecorated C identifiers.");
	}
}

void ContextDocument::RequireChoice(const Json& Document, std::string_view Key,
                                    std::initializer_list<std::string_view> Choices)
{
	const std::string Value = Text(Document, Key);
	if (std::find(Choices.begin(), Choices.end(), std::string_view(Value)) == Choices.end())
	{
		throw std::runtime_error("Unsupported " + std::string(Key) + ": " + Value);
	}
}
} // namespace Mod::BuildTool
