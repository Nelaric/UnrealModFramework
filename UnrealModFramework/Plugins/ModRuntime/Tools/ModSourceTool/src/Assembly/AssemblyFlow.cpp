// Copyright (c) 2026 Nelaric Contributors
#include "AssemblyFlow.h"

#include "ModSourceTool/src/Assembly/ListingText.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::SourceTool
{
std::string_view AssemblyFlow::BranchLabel(const Instruction& Branch)
{
	return ListingText::Trim(Branch.Operands.substr(Branch.Operands.find_last_of(" \t") + 1));
}

bool AssemblyFlow::IsReturnRegister(std::string_view Register)
{
	return Register == "al" || Register == "ax" || Register == "eax" || Register == "rax";
}

bool AssemblyFlow::IsTrapBlock(const std::vector<Instruction>& Body, std::string_view Label)
{
	const auto Start = std::find_if(Body.begin(), Body.end(), [&](const Instruction& Instruction)
	                                { return Instruction.Operation == "label" && Instruction.Operands == Label; });
	if (Start == Body.end())
	{
		return false;
	}
	for (auto Instruction = Start + 1; Instruction != Body.end(); ++Instruction)
	{
		const auto Op = Instruction->Operation;
		if (Op == "int3" || Op == "ud2")
		{
			return true;
		}
		if (Op != "mov" && Op != "lea" && Op != "xor" &&
		    !(Op == "call" && BranchLabel(*Instruction) == "__security_check_cookie"))
		{
			return false;
		}
	}
	return false;
}

bool AssemblyFlow::ReturnsFalse(const std::vector<Instruction>& Body, std::string_view Label, bool bZero)
{
	const auto Start = std::find_if(Body.begin(), Body.end(), [&](const Instruction& Instruction)
	                                { return Instruction.Operation == "label" && Instruction.Operands == Label; });
	if (Start == Body.end())
	{
		return false;
	}
	for (auto Instruction = Start + 1; Instruction != Body.end(); ++Instruction)
	{
		const auto Op = Instruction->Operation;
		const auto [Destination, Value] = ListingText::Split(Instruction->Operands, Instruction->Operands.find(','));
		const bool bResult = IsReturnRegister(Destination);
		if (Op == "ret")
		{
			return bZero;
		}
		if (Op == "xor" && bResult && Destination == Value)
		{
			bZero = true;
		}
		else if (Op == "mov" && bResult)
		{
			bZero = ListingText::Unsigned(Value) == 0;
		}
		else if (Op == "cmp" || Op == "test" ||
		         (Op.starts_with('j') && Op != "jmp" && IsTrapBlock(Body, BranchLabel(*Instruction))))
		{
			continue;
		}
		else if ((Op != "mov" && Op != "pop" && Op != "nop" && Op != "label" && Op != "xor" &&
		          !((Op == "add" || Op == "lea") && Destination == "rsp")) ||
		         bResult || Destination == "ah")
		{
			return false;
		}
	}
	return false;
}

bool AssemblyFlow::BelongsToGuard(const std::vector<Instruction>& Body, std::size_t First, std::size_t Last,
                                  const std::string& File, unsigned Line)
{
	for (auto Before = First; Before > 0;)
	{
		const auto& Instruction = Body[--Before];
		if (Instruction.Operation != "label" && Instruction.File == File && Instruction.Line != 0)
		{
			if (Instruction.Line == Line)
			{
				return true;
			}
			break;
		}
	}
	for (auto After = Last + 1; After < Body.size(); ++After)
	{
		const auto& Instruction = Body[After];
		if (Instruction.Operation != "label" && Instruction.File == File && Instruction.Line != 0)
		{
			return Instruction.Line == Line;
		}
	}
	return false;
}

bool AssemblyFlow::ZeroBeforeBranch(const std::vector<Instruction>& Body, std::size_t Branch)
{
	while (Branch != 0)
	{
		const auto& Instruction = Body[--Branch];
		const auto Op = Instruction.Operation;
		const auto [Destination, Value] = ListingText::Split(Instruction.Operands, Instruction.Operands.find(','));
		if (Op == "test" || Op == "cmp" || (Op.starts_with('j') && Op != "jmp"))
		{
			continue;
		}
		if (Op != "mov" && Op != "xor" && Op != "and" && Op != "or")
		{
			return false;
		}
		if (IsReturnRegister(Destination))
		{
			return (Op == "xor" && Destination == Value) || (Op == "mov" && ListingText::Unsigned(Value) == 0);
		}
	}
	return false;
}
} // namespace Mod::SourceTool
