// Copyright (c) 2026 Nelaric Contributors
#include "Generation.h"

#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Files/TextFile.h"
#include "ModHeaderTool/src/Generation/APIGenerator.h"
#include "ModHeaderTool/src/Generation/GenerationFields.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

void Generation::Generate(const fs::path& RequestFile)
{
	const auto File = fs::canonical(RequestFile);
	const auto Request = nlohmann::json::parse(TextFile::ReadTextFile(File));
	if (Request.value("format", "") != "UMF.ModHeaderGenerationRequest" || Request.value("schemaVersion", 0) != 1)
	{
		throw std::runtime_error("Expected a UMF.ModHeaderGenerationRequest v1 document.");
	}
	const auto Root = File.parent_path();
	const auto ReadPath = [&](const char* Key, bool bRequired) -> fs::path
	{
		const auto Value = bRequired ? GenerationFields::Required(Request, Key) : Request.value(Key, std::string{});
		if (Value.find('\0') != std::string::npos)
		{
			throw std::runtime_error(std::string("Invalid generation request path: ") + Key);
		}
		return Value.empty() ? fs::path{} : Root / Path::PathFromUtf8(Value);
	};
	APIGenerator(ReadPath("buildInput", true), ReadPath("hostContexts", true))
	    .Generate(ReadPath("outputDirectory", true), ReadPath("metadataOptions", false), ReadPath("libclang", false),
	              ReadPath("lifecycleSource", false), ReadPath("blueprintSnapshot", false));
}
} // namespace Mod::HeaderTool
