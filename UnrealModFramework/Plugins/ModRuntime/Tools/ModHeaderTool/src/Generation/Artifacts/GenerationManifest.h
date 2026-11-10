// Copyright (c) 2026 Nelaric Contributors
/** @file GenerationManifest.h */
#pragma once

#include "ModHeaderTool/src/Generation/Artifacts/GeneratedBatch.h"
#include "ModHeaderTool/src/Generation/Artifacts/InputIndex.h"
#include "ModHeaderTool/src/Generation/Artifacts/OutputIndex.h"
#include "ModHeaderTool/src/Generation/GenerationModel.h"

#include <map>
#include <nlohmann/json.hpp>
#include <string>

namespace Mod::HeaderTool
{
using Json = nlohmann::json;
using Files = std::map<std::string, std::string>;

class GenerationManifest
{
public:
	static Json MakeManifest(const GenerationModel& Model, const GeneratedBatch& Batch);

private:
	static constexpr const char* GeneratorVersion = "0.5.0-verified-host";
	static InputIndex DescribeInputs(const GenerationModel& Model);
	static OutputIndex DescribeOutputs(const Files& Files, const std::string& ErrorHeader);
	static Json ReadHostContextHashes(const GenerationModel& Model);
	static Json DescribeWatches(const GenerationModel& Model);
};
} // namespace Mod::HeaderTool
