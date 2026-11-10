// Copyright (c) 2026 Nelaric Contributors
#include "AssemblyEvidence.h"

#include "ModSourceTool/src/Assembly/AssemblyCalls.h"
#include "ModSourceTool/src/Assembly/AssemblyFlow.h"
#include "ModSourceTool/src/Assembly/FlagBranches.h"
#include "ModSourceTool/src/Assembly/ValidityBranches.h"
#include "ModSourceTool/src/Assembly/ValidityMatcher.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

void AssemblyEvidence::SetEvidence(Json& Failure, const Json& Evidence)
{
	for (auto& Classification : Failure.at("classifications"))
	{
		Classification["assembly"] = Evidence;
	}
}

void AssemblyEvidence::AnalyzeGasValidity(Json& Failure, const std::vector<Instruction>& Body,
                                          const DebugLocations& Locations, std::uint64_t Flag)
{
	const auto Call = Failure.at("validityCall");
	Failure.erase("validityCall");
	const auto& Condition = Failure.at("conditionLocation");
	const auto File = Condition.at("file").get<std::string>();
	const auto Line = Condition.at("line").get<unsigned>();
	std::vector<FlagBranches> Matches;
	std::vector<std::string_view> Calls;
	for (std::size_t Index = 0; Index < Body.size(); ++Index)
	{
		const auto& Instruction = Body[Index];
		if (Instruction.Operation == "call" && Instruction.File == File && Instruction.Line == Line &&
		    AssemblyCalls::CalledSymbol(Instruction) == Call.at("symbol").get_ref<const std::string&>())
		{
			Calls.push_back(Instruction.Text);
		}
		if (!std::has_single_bit(Flag) ||
		    (!Locations.Sites.empty() &&
		     !ValidityMatcher::InlineSiteMatches(Instruction.Site, Locations, Call, Condition)))
		{
			continue;
		}
		if (const auto Match = ValidityMatcher::MatchGasValidity(Body, Index, Flag);
		    Match && (!Locations.Sites.empty() ||
		              (Instruction.File.find("/Runtime/Core") != std::string::npos &&
		               AssemblyFlow::BelongsToGuard(Body, Match->NullTest, Match->FlagBranch, File, Line))))
		{
			Matches.push_back(*Match);
		}
	}
	if (Matches.size() == 1 && Calls.empty())
	{
		const auto Match = Matches.front();
		Json NullInstructions = Json::array();
		for (auto Index = Match.NullTest; Index <= Match.NullBranch; ++Index)
		{
			NullInstructions.push_back(Body[Index].Text);
		}
		auto& Reasons = Failure.at("classifications");
		Reasons.at(0)["assembly"] = {{"kind", "inline"},
		                             {"failureLabel", AssemblyFlow::BranchLabel(Body[Match.NullBranch])},
		                             {"instructions", std::move(NullInstructions)}};
		Reasons.at(1)["assembly"] = {
		    {"kind", "inline"},
		    {"failureLabel", AssemblyFlow::BranchLabel(Body[Match.FlagBranch])},
		    {"flag", Flag},
		    {"instructions", {Body[Match.FlagLoad].Text, Body[Match.FlagTest].Text, Body[Match.FlagBranch].Text}}};
	}
	else if (Matches.empty() && Calls.size() == 1)
	{
		SetEvidence(Failure, {{"kind", "call"}, {"symbol", Call.at("symbol")}, {"instructions", Calls}});
	}
	else
	{
		SetEvidence(Failure, {{"kind", "unresolved"}, {"reason", "No unique validity path in the assembly listing."}});
	}
}

void AssemblyEvidence::AnalyzeValidity(Json& Failure, const std::vector<Instruction>& Body, const std::string& File,
                                       std::uint64_t Flag, const std::map<unsigned, unsigned>& CallsByLine)
{
	const auto Call = Failure.at("validityCall");
	Failure.erase("validityCall");
	const auto Header = Call.at("file").get<std::string>();
	const auto Symbol = Call.at("symbol").get<std::string>();
	const auto Line = Failure.at("conditionLocation").at("line").get<unsigned>();
	std::vector<ValidityBranches> Matches;
	std::vector<std::string_view> Calls;
	for (std::size_t Index = 0; Index < Body.size(); ++Index)
	{
		const auto& Instruction = Body[Index];
		if (!Symbol.empty() && Instruction.Operation == "call" && Instruction.File == File &&
		    Instruction.Line == Line && AssemblyFlow::BranchLabel(Instruction).ends_with(Symbol))
		{
			Calls.push_back(Instruction.Text);
		}
		if (std::has_single_bit(Flag) && Index + 4 < Body.size())
		{
			if (const auto Match = ValidityMatcher::MatchValidity(Body, Index, Header, Flag);
			    Match && AssemblyFlow::BelongsToGuard(Body, Match->First, Match->Last, File, Line) &&
			    std::count_if(
			        CallsByLine.begin(), CallsByLine.end(), [&](const auto& Entry)
			        { return AssemblyFlow::BelongsToGuard(Body, Match->First, Match->Last, File, Entry.first); }) == 1)
			{
				Matches.push_back(*Match);
			}
		}
	}
	if (Matches.size() == 1 && Calls.empty())
	{
		const auto [First, Last] = Matches.front();
		auto& Classifications = Failure.at("classifications");
		const auto Target = AssemblyFlow::BranchLabel(Body[Last]);
		Json GarbageInstructions = Json::array();
		for (auto Index = First + 2; Index <= Last; ++Index)
		{
			GarbageInstructions.push_back(Body[Index].Text);
		}
		Classifications.at(0)["assembly"] = {{"kind", "inline"},
		                                     {"failureLabel", Target},
		                                     {"source", {{"file", Body[First].File}, {"line", Body[First].Line}}},
		                                     {"instructions", {Body[First].Text, Body[First + 1].Text}}};
		Classifications.at(1)["assembly"] = {{"kind", "inline"},
		                                     {"failureLabel", Target},
		                                     {"source", {{"file", Body[Last].File}, {"line", Body[Last].Line}}},
		                                     {"flag", Flag},
		                                     {"instructions", std::move(GarbageInstructions)}};
	}
	else if (Matches.empty() && Calls.size() == 1)
	{
		SetEvidence(Failure, {{"kind", "call"}, {"symbol", Symbol}, {"instructions", Calls}});
	}
	else
	{
		SetEvidence(Failure, {{"kind", "unresolved"}, {"reason", "No unique validity check in the assembly listing."}});
	}
}
} // namespace Mod::SourceTool
