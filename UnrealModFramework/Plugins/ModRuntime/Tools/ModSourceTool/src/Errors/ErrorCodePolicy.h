// Copyright (c) 2026 Nelaric Contributors
/** @file ErrorCodePolicy.h */
#pragma once

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

namespace Mod::SourceTool
{
class ErrorCodePolicy
{
public:
	ErrorCodePolicy();
	void Bind(nlohmann::json& Failures);

private:
	std::map<std::string, std::uint32_t> Codes;
	std::map<std::uint32_t, std::string> Names;
};
} // namespace Mod::SourceTool
