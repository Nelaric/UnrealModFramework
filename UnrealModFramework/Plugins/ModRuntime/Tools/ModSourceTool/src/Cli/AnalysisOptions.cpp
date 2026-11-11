// Copyright (c) 2026 Nelaric Contributors
#include "AnalysisOptions.h"

#include "Common/Files/File.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace Mod::SourceTool
{
AnalysisOptions AnalysisOptions::Read(const std::filesystem::path& File)
{
	const auto Document = nlohmann::json::parse(Mod::BuildTool::File::ReadFile(File));
	if (Document.value("format", "") != "UMF.SourceAnalysisRequest" || Document.value("schemaVersion", 0) != 1)
	{
		throw std::runtime_error("Expected a UMF.SourceAnalysisRequest v1 document.");
	}
	const auto Text = [&](const char* Key)
	{
		const auto Value = Document.at(Key).get<std::string>();
		if (Value.empty() || Value.find('\0') != std::string::npos)
		{
			throw std::runtime_error(std::string("Expected a nonempty source analysis request field: ") + Key);
		}
		return Value;
	};
	return {
	    .Catalog = Text("catalog"),
	    .MemberId = Text("member"),
	    .CompilationDatabase = Text("compilationDatabase"),
	    .Target = Text("target"),
	    .ClangLibrary = Text("libclang"),
	    .ResourceDirectory = Text("resourceDirectory"),
	    .AnalysisArguments = Document.at("analysisArguments").get<std::vector<std::string>>(),
	};
}
} // namespace Mod::SourceTool
