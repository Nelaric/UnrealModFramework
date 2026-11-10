// Copyright (c) 2026 Nelaric Contributors
#include "BuildProgress.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <string_view>

namespace Mod::SdkTool
{
using Json = nlohmann::json;

void BuildProgress::ReportStage(std::string_view Stage, std::string_view Message)
{
	std::cerr
	    << Json({{"format", "UMF.SdkProgress"}, {"schemaVersion", 1}, {"stage", Stage}, {"message", Message}}).dump()
	    << '\n';
}
} // namespace Mod::SdkTool
