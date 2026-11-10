// Copyright (c) 2026 Nelaric Contributors
#include "ValidityMatcher.h"

#include "ModSourceTool/src/Assembly/AssemblyFlow.h"
#include "ModSourceTool/src/Assembly/InlineSite.h"
#include "ModSourceTool/src/Assembly/ListingText.h"

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

std::string_view ValidityMatcher::ObjectRegister(std::string_view Address)
{
	constexpr std::string_view Prefix = "DWORD PTR [";
	if ((!Address.starts_with(Prefix) && !Address.starts_with("dword ptr [")) || !Address.ends_with(']'))
	{
		return {};
	}
	Address.remove_prefix(Prefix.size());
	const auto [Base, Offset] = ListingText::Split(Address.substr(0, Address.size() - 1), Address.find('+'));
	return ListingText::Unsigned(Offset).has_value() ? Base : std::string_view{};
}

std::optional<ValidityBranches> ValidityMatcher::MatchValidity(const std::vector<Instruction>& Body, std::size_t First,
                                                               std::string_view Header, std::uint64_t Flag)
{
	const auto& NullTest = Body[First];
	const auto& NullBranch = Body[First + 1];
	const auto [Pointer, Other] = ListingText::Split(NullTest.Operands, NullTest.Operands.find(','));
	if (NullTest.Operation != "test" || Pointer.empty() || Pointer != Other || !Pointer.starts_with('r') ||
	    NullTest.File != Header || NullTest.Source.find("CheckObjectValidBasedOnItsFlags") == std::string_view::npos ||
	    (NullBranch.Operation != "je" && NullBranch.Operation != "jz"))
	{
		return std::nullopt;
	}
	const auto& Read = Body[First + 2];
	const auto& BitTest = Body[First + 3];
	const auto [FlagRegister, Address] = ListingText::Split(Read.Operands, Read.Operands.find(','));
	const auto [BitRegister, Bit] = ListingText::Split(BitTest.Operands, BitTest.Operands.find(','));
	const auto& CarryBranch = Body[First + 4];
	std::size_t Last = 0;
	if (Read.Operation == "mov" && ObjectRegister(Address) == Pointer && BitTest.Operation == "bt" &&
	    FlagRegister == BitRegister && ListingText::Unsigned(Bit) == static_cast<unsigned>(std::countr_zero(Flag)) &&
	    (CarryBranch.Operation == "jb" || CarryBranch.Operation == "jc" || CarryBranch.Operation == "jnae"))
	{
		Last = First + 4;
	}
	else if (Read.Operation == "test" && ObjectRegister(FlagRegister) == Pointer &&
	         ListingText::Unsigned(Address) == Flag && (BitTest.Operation == "jne" || BitTest.Operation == "jnz"))
	{
		Last = First + 3;
	}
	if (Last == 0 || Body[Last].File != Header ||
	    AssemblyFlow::BranchLabel(Body[Last]) != AssemblyFlow::BranchLabel(NullBranch) ||
	    !AssemblyFlow::ReturnsFalse(Body, AssemblyFlow::BranchLabel(NullBranch)))
	{
		return std::nullopt;
	}
	return ValidityBranches{First, Last};
}

bool ValidityMatcher::InlineSiteMatches(unsigned SiteId, const DebugLocations& Locations, const Json& Call,
                                        const Json& Condition)
{
	for (auto It = Locations.Sites.find(SiteId); It != Locations.Sites.end();
	     It = Locations.Sites.find(It->second.Parent))
	{
		const auto& Site = It->second;
		if (Locations.File(Site.OriginFile) == Call.at("file").get_ref<const std::string&>() &&
		    Site.OriginLine == Call.at("line").get<unsigned>() &&
		    Locations.File(Site.CallerFile) == Condition.at("file").get_ref<const std::string&>() &&
		    Site.CallerLine == Condition.at("line").get<unsigned>())
		{
			return true;
		}
		if (It->first == Site.Parent)
		{
			break;
		}
	}
	return false;
}

std::optional<FlagBranches> ValidityMatcher::MatchGasValidity(const std::vector<Instruction>& Body, std::size_t Load,
                                                              std::uint64_t Flag)
{
	const auto& Read = Body[Load];
	const auto [FlagRegister, Address] = ListingText::Split(Read.Operands, Read.Operands.find(','));
	const auto Pointer = ObjectRegister(Address);
	if (Read.Operation != "mov" || Pointer.empty())
	{
		return std::nullopt;
	}
	std::size_t FlagTest = 0;
	for (auto I = Load + 1; I + 1 < Body.size() && I < Load + 12; ++I)
	{
		const auto& Candidate = Body[I];
		const auto [Destination, Value] = ListingText::Split(Candidate.Operands, Candidate.Operands.find(','));
		if (Candidate.Operation == "call" || Candidate.Operation == "jmp" || Candidate.Operation == "ret" ||
		    Candidate.Operation == "label")
		{
			break;
		}
		if (Destination == FlagRegister)
		{
			if ((Candidate.Operation == "test" || Candidate.Operation == "and") &&
			    ListingText::Unsigned(Value) == Flag &&
			    (Body[I + 1].Operation == "jne" || Body[I + 1].Operation == "jnz") &&
			    AssemblyFlow::ReturnsFalse(Body, AssemblyFlow::BranchLabel(Body[I + 1]),
			                               AssemblyFlow::ZeroBeforeBranch(Body, I + 1)))
			{
				FlagTest = I;
			}
			break;
		}
	}
	if (FlagTest == 0)
	{
		return std::nullopt;
	}
	for (auto I = Load; I > 0 && Load - I < 12;)
	{
		const auto& Candidate = Body[--I];
		const auto [Left, Right] = ListingText::Split(Candidate.Operands, Candidate.Operands.find(','));
		if (Candidate.Operation == "call" || Candidate.Operation == "jmp" || Candidate.Operation == "label")
		{
			break;
		}
		if (Candidate.Operation == "test" && Left == Pointer && Right == Pointer)
		{
			auto Branch = I + 1;
			if (Branch + 2 < Load && (Body[Branch].Operation == "sete" || Body[Branch].Operation == "setz") &&
			    Body[Branch + 1].Operation == "or" &&
			    ListingText::Split(Body[Branch + 1].Operands, Body[Branch + 1].Operands.find(',')).first ==
			        Body[Branch].Operands &&
			    (Body[Branch + 2].Operation == "jne" || Body[Branch + 2].Operation == "jnz"))
			{
				Branch += 2;
			}
			else if (Body[Branch].Operation != "je" && Body[Branch].Operation != "jz")
			{
				return std::nullopt;
			}
			if (AssemblyFlow::ReturnsFalse(Body, AssemblyFlow::BranchLabel(Body[Branch]),
			                               AssemblyFlow::ZeroBeforeBranch(Body, Branch)))
			{
				return FlagBranches{I, Branch, Load, FlagTest, FlagTest + 1};
			}
			break;
		}
		if (Left == Pointer && Candidate.Operation != "cmp" && Candidate.Operation != "test")
		{
			break;
		}
	}
	return std::nullopt;
}
} // namespace Mod::SourceTool
