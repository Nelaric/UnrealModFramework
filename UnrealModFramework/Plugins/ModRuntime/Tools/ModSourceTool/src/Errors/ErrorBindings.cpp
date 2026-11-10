// Copyright (c) 2026 Nelaric Contributors
#include "ErrorBindings.h"

#include "ModSourceTool/src/Errors/CatalogText.h"

#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

[[noreturn]] void ErrorBindings::FailGuard(const Json& Failure, const std::string& Message)
{
	const auto& Location = Failure.at("conditionLocation");
	throw std::runtime_error(CatalogText::Text(Location, "file") + "(" + Location.at("line").dump() + "," +
	                         Location.at("column").dump() + "): " + Message + " Guard: '" +
	                         CatalogText::Text(Failure, "guard") + "'.");
}

std::map<std::string, std::string> ErrorBindings::IndexGuardReasons(const Json& Reasons)
{
	std::map<std::string, std::string> ReasonByGuard;
	for (const auto& [Key, Reason] : Reasons.items())
	{
		const auto& Guards = Reason.at("guards");
		if (!Guards.is_array())
		{
			throw std::runtime_error("Expected a guards array for " + Key);
		}
		for (const auto& Guard : Guards)
		{
			const auto Predicate = Guard.get<std::string>();
			if (Predicate.empty() || !ReasonByGuard.emplace(Predicate, Key).second)
			{
				throw std::runtime_error("Empty or duplicate guard for " + Key + ": " + Predicate);
			}
		}
	}
	return ReasonByGuard;
}

void ErrorBindings::MatchClassifications(const Json& Failure, Json& Classifications, Json& ClassifiedReasons,
                                         bool bUpdate, PendingBindings& Pending)
{
	for (auto& Classification : Classifications)
	{
		const auto Key = CatalogText::Text(Classification, "key");
		const auto Existing = ClassifiedReasons.find(Key);
		if (Existing == ClassifiedReasons.end())
		{
			if (!bUpdate)
			{
				FailGuard(Failure, "Recognized " + Key + ". Run with --update-catalog to assign its code.");
			}
			Json Reason = Classification;
			Reason.erase("key");
			Reason.erase("assembly");
			ClassifiedReasons.emplace(Key, std::move(Reason));
		}
		else if (Existing->at("macro") != Classification.at("macro") ||
		         Existing->value("type", "") != Classification.value("type", ""))
		{
			FailGuard(Failure, "Catalog metadata disagrees with the classification " + Key + '.');
		}
		if (!ClassifiedReasons.at(Key).contains("code"))
		{
			Pending.Unassigned.emplace(Key, true);
		}
		Json BoundFailure = Failure;
		BoundFailure["key"] = Key;
		BoundFailure["classification"] = std::move(Classification);
		Pending.Failures.push_back(std::move(BoundFailure));
	}
}

PendingBindings ErrorBindings::MatchReasons(Json& Failures, const Json& Reasons, Json& ClassifiedReasons,
                                            bool bStructured, bool bUpdate)
{
	const auto ReasonByGuard = IndexGuardReasons(Reasons);
	PendingBindings Pending;
	for (auto& Failure : Failures)
	{
		const auto Predicate = CatalogText::Text(Failure, "guard");
		const auto Found = ReasonByGuard.find(Predicate);
		auto Classifications = Failure.value("classifications", Json::array());
		Failure.erase("classifications");
		if (Found != ReasonByGuard.end())
		{
			const auto& Key = Found->second;
			Failure["key"] = Key;
			if (!Reasons.at(Key).contains("code"))
			{
				Pending.Unassigned.emplace(Key, false);
			}
			Pending.Failures.push_back(std::move(Failure));
			continue;
		}
		if (!bStructured || Classifications.empty())
		{
			FailGuard(Failure, "No supported classification. Add an explicit reason for this guard.");
		}
		MatchClassifications(Failure, Classifications, ClassifiedReasons, bUpdate, Pending);
	}
	return Pending;
}

void ErrorBindings::ApplyAssignedCodes(Json& BoundFailures, const Json& Reasons, const Json& ClassifiedReasons,
                                       bool bStructured)
{
	for (auto& Failure : BoundFailures)
	{
		const auto& Store = Failure.contains("classification") ? ClassifiedReasons : Reasons;
		const auto& Reason = Store.at(CatalogText::Text(Failure, "key"));
		Failure["code"] = Reason.at("code");
		if (bStructured)
		{
			Failure["macro"] = Reason.at("macro");
		}
	}
}
} // namespace Mod::SourceTool
