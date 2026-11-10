// Copyright (c) 2026 Nelaric Contributors
#include "AssemblyCalls.h"

#include "ModSourceTool/src/Assembly/AssemblyEvidence.h"
#include "ModSourceTool/src/Assembly/AssemblyFlow.h"

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

std::string_view AssemblyCalls::CalledSymbol(const Instruction& Instruction)
{
	auto Target = AssemblyFlow::BranchLabel(Instruction);
	if (Target.starts_with('"') && Target.ends_with('"') && Target.size() > 1)
	{
		Target = Target.substr(1, Target.size() - 2);
	}
	if (Target.starts_with("__imp_"))
	{
		Target.remove_prefix(6);
	}
	if (Target.ends_with("@PLT") || Target.ends_with("@plt"))
	{
		Target.remove_suffix(4);
	}
	return Target;
}

bool AssemblyCalls::At(const Instruction& Instruction, const Json& Location)
{
	return Instruction.File == Location.at("file").get_ref<const std::string&>() &&
	       Instruction.Line == Location.at("line").get<unsigned>();
}

void AssemblyCalls::AnalyzeCallPath(Json& Failure, const std::vector<Instruction>& Body)
{
	const auto& Chain = Failure.at("callChain");
	const bool bThrow = Failure.value("exitKind", "") == "throw" || Failure.value("exitKind", "") == "terminate";
	std::vector<std::string_view> Direct;
	std::vector<std::string_view> Inlined;
	for (const auto& Instruction : Body)
	{
		if (Instruction.Operation == "call" || Instruction.Operation == "jmp")
		{
			const auto Target = CalledSymbol(Instruction);
			if (!Chain.empty() && At(Instruction, Chain.front().at("location")) &&
			    Target == Chain.front().at("symbol").get_ref<const std::string&>())
			{
				Direct.push_back(Instruction.Text);
			}
			if (bThrow && At(Instruction, Failure.at("returnLocation")) &&
			    (Target == "_CxxThrowException" || Target == "__cxa_throw"))
			{
				Inlined.push_back(Instruction.Text);
			}
		}
		if (!bThrow && !Chain.empty() && At(Instruction, Failure.at("conditionLocation")) &&
		    !At(Instruction, Chain.front().at("location")) && Instruction.Operation != "label")
		{
			Inlined.push_back(Instruction.Text);
		}
	}
	if (Direct.size() == 1)
	{
		AssemblyEvidence::SetEvidence(
		    Failure, {{"kind", "call"}, {"symbol", Chain.front().at("symbol")}, {"instructions", Direct}});
	}
	else if (Direct.empty() && !Inlined.empty())
	{
		AssemblyEvidence::SetEvidence(Failure,
		                              {{"kind", Chain.empty() ? "throw" : "inline"}, {"instructions", Inlined}});
	}
	else
	{
		AssemblyEvidence::SetEvidence(
		    Failure, {{"kind", "unresolved"}, {"reason", "No unique call or inline location in the listing."}});
	}
}
} // namespace Mod::SourceTool
