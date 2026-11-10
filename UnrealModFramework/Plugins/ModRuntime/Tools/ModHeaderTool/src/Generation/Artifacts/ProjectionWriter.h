// Copyright (c) 2026 Nelaric Contributors
/** @file ProjectionWriter.h */
#pragma once

#include "ModHeaderTool/src/Generation/API.h"
#include "ModHeaderTool/src/Generation/APIType.h"
#include "ModHeaderTool/src/Generation/Artifacts/Projection.h"
#include "ModHeaderTool/src/Generation/GenerationModel.h"

#include <nlohmann/json.hpp>

namespace Mod::HeaderTool
{
using Json = nlohmann::json;

class ProjectionWriter
{
public:
	static Json APIJson(const GenerationModel& Model, const API& API);
	static Projection BuildProjection(GenerationModel& Model);

private:
	static Json TypeJson(const GenerationModel& Model, const APIType& Type);
	static Json DescribeTypes(GenerationModel& Model);
};
} // namespace Mod::HeaderTool
