// Copyright (c) 2026 Nelaric Contributors
#include "ErrorCodePolicy.h"

#include "Common/Errors/PublicStatuses.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Mod::SourceTool
{
namespace
{
using Json = nlohmann::json;
using Tools::PublicStatuses;

const PublicStatuses::PublicStatus* FindStatus(std::string_view Key)
{
	const auto Find = [Key](const auto& Statuses) -> const PublicStatuses::PublicStatus*
	{
		const auto It = std::ranges::find_if(Statuses, [Key](const auto& Status) { return Status.Key == Key; });
		return It == std::end(Statuses) ? nullptr : &*It;
	};
	if (const auto* Framework = Find(PublicStatuses::FrameworkStatuses))
	{
		return Framework;
	}
	if (const auto* Adapter = Find(PublicStatuses::AdapterStatuses))
	{
		return Adapter;
	}
	return Find(PublicStatuses::SourceStatuses);
}

[[noreturn]] void FailAt(const Json& Failure, const std::string& Message)
{
	const auto& Loc = Failure.at("conditionLocation");
	throw std::runtime_error(Loc.at("file").get<std::string>() + "(" + Loc.at("line").dump() + "," +
	                         Loc.at("column").dump() + "): " + Message);
}
} // namespace

ErrorCodePolicy::ErrorCodePolicy()
{
	const auto Reserve = [&](const auto& Statuses)
	{
		for (const auto& Status : Statuses)
		{
			Codes.emplace(Status.Macro, Status.Code);
			Names.emplace(Status.Code, Status.Macro);
		}
	};
	Reserve(PublicStatuses::FrameworkStatuses);
	Reserve(PublicStatuses::AdapterStatuses);
	Reserve(PublicStatuses::SourceStatuses);
}

void ErrorCodePolicy::Bind(Json& Failures)
{
	Json Bound = Json::array();
	for (auto& Failure : Failures)
	{
		auto Classifications = Failure.value("classifications", Json::array());
		Failure.erase("classifications");
		if (Classifications.empty())
		{
			FailAt(Failure, "Declare a stable business code at this return using MOD_SOURCE_FAILURE.");
		}
		for (auto& Classification : Classifications)
		{
			const auto Key = Classification.at("key").get<std::string>();
			const auto Macro = Classification.at("macro").get<std::string>();
			std::uint32_t Code = 0;
			if (Classification.contains("code"))
			{
				const auto& Value = Classification.at("code");
				const auto Number = Value.is_number_integer() ? Value.get<std::uint64_t>() : 0;
				if (!Tools::ErrorCodes::IsErrorMacro(Macro) || Key != Macro ||
				    Number > std::numeric_limits<std::uint32_t>::max() ||
				    (Number >> 28) != static_cast<std::uint64_t>(Tools::ErrorCodes::EErrorDomain::Business) ||
				    (Number & Tools::ErrorCodes::ErrorDetailMask) == 0)
				{
					FailAt(Failure, "A declared business error needs a MOD_ERROR_* name and a fixed Business code.");
				}
				Code = static_cast<std::uint32_t>(Number);
			}
			else
			{
				const auto* Status = FindStatus(Key);
				if (!Status || Macro != Status->Macro || Status->Code == 0)
				{
					FailAt(Failure, "Unknown fixed source error: " + Key);
				}
				Code = Status->Code;
			}
			const auto [KnownCode, bNewName] = Codes.emplace(Macro, Code);
			const auto [KnownName, bNewCode] = Names.emplace(Code, Macro);
			if ((!bNewName && KnownCode->second != Code) || (!bNewCode && KnownName->second != Macro))
			{
				FailAt(Failure, "Conflicting source error code or name: " + Macro);
			}
			Json Entry = Failure;
			Entry["key"] = Key;
			Entry["macro"] = Macro;
			Entry["code"] = Code;
			Entry["classification"] = std::move(Classification);
			Bound.push_back(std::move(Entry));
		}
	}
	Failures = std::move(Bound);
}
} // namespace Mod::SourceTool
