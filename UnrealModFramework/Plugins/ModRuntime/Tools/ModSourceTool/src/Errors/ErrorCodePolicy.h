// Copyright (c) 2026 Nelaric Contributors
/** @file ErrorCodePolicy.h */
#pragma once

#include "Common/Errors/PublicStatuses.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

class ErrorCodePolicy
{
public:
	static void ValidateLegacyCodes(const Json& Members, const Json& Counter);
	static void ValidateStructuredCodes(const Json& Members, const Json& ClassifiedReasons, const Json& Counters);
	static std::uint32_t AllocateCode(Json& Counters, const Json& Reason, const std::string& Key, bool bStructured);

private:
	static constexpr std::int64_t FirstBusinessCode = 0x01000000;
	static constexpr std::int64_t LastBusinessCode = 0x7FFFFFFF;
	static constexpr std::int64_t WrongThreadCode = 1;
	static std::uint32_t ErrorType(std::string_view Type);
	static const Tools::PublicStatuses::PublicStatus* FindFrameworkStatus(std::string_view Key);
};
} // namespace Mod::SourceTool
