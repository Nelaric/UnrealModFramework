// Copyright (c) 2026 Nelaric Contributors
#include "LifecycleExport.h"

#include "ModHeaderTool/src/Files/TextFile.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

std::string LifecycleExport::ExportLifecycleDeclarations(const fs::path& Source)
{
	Json Declarations = Json::object();
	for (const std::string Name : LifecycleClasses)
	{
		const std::string Header = TextFile::ReadTextFile(Source / (Name + ".h"));
		const std::string GeneratedInclude = "#include \"" + Name + ".generated.h\"";
		const auto Include = Header.find(GeneratedInclude);
		const auto LineEnd = Include == std::string::npos ? Include : Header.find('\n', Include);
		const auto Body = LineEnd == std::string::npos ? LineEnd : Header.find_first_not_of("\r\n", LineEnd);
		if (Body == std::string::npos || Header.size() > 1024 * 1024)
		{
			throw std::runtime_error("Invalid reflected Mod lifecycle header: " + Name);
		}
		std::string Declaration = Header.substr(Body);
		std::erase(Declaration, '\r');
		Declarations[Name] = std::move(Declaration);
	}
	return Json{{"format", "UMF.ModLifecycleExport"}, {"schemaVersion", 1}, {"declarations", Declarations}}.dump(2) +
	       '\n';
}
} // namespace Mod::HeaderTool
