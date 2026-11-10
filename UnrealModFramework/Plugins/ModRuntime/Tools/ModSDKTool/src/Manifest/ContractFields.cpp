// Copyright (c) 2026 Nelaric Contributors
#include "ContractFields.h"

#include <nlohmann/json.hpp>

namespace Mod::SdkTool
{
using Json = nlohmann::json;

const char* ContractFields::AdapterABIKey(const Json& Document)
{
	return Document.at("schemaVersion") == 1 ? "adapterA"
	                                           "bi"
	                                         : "adapterABI";
}
} // namespace Mod::SdkTool
