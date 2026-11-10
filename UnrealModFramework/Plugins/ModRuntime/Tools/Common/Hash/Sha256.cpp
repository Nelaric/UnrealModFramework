// Copyright (c) 2026 Nelaric Contributors
#include "Sha256.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Mod::BuildTool
{
void Sha256::Update(std::string_view Bytes)
{
	ByteCount += Bytes.size();
	for (unsigned char Byte : Bytes)
	{
		Block[Used++] = Byte;
		if (Used == Block.size())
		{
			Transform();
			Used = 0;
		}
	}
}

std::string Sha256::Finish()
{
	const std::uint64_t Bits = ByteCount * 8;
	Block[Used++] = 0x80;
	if (Used > 56)
	{
		while (Used < 64)
		{
			Block[Used++] = 0;
		}
		Transform();
		Used = 0;
	}
	while (Used < 56)
	{
		Block[Used++] = 0;
	}
	for (int Index = 7; Index >= 0; --Index)
	{
		Block[Used++] = static_cast<unsigned char>(Bits >> (Index * 8));
	}
	Transform();
	constexpr char Digits[] = "0123456789abcdef";
	std::string Result;
	for (std::uint32_t Word : State)
	{
		for (int Shift = 28; Shift >= 0; Shift -= 4)
		{
			Result.push_back(Digits[(Word >> Shift) & 15]);
		}
	}
	return Result;
}

void Sha256::Transform()
{
	std::array<std::uint32_t, 64> Words{};
	for (std::size_t Index = 0; Index < 16; ++Index)
	{
		Words[Index] = (std::uint32_t(Block[Index * 4]) << 24) | (std::uint32_t(Block[Index * 4 + 1]) << 16) |
		               (std::uint32_t(Block[Index * 4 + 2]) << 8) | Block[Index * 4 + 3];
	}
	for (std::size_t Index = 16; Index < 64; ++Index)
	{
		const std::uint32_t First =
		    std::rotr(Words[Index - 15], 7) ^ std::rotr(Words[Index - 15], 18) ^ (Words[Index - 15] >> 3);
		const std::uint32_t Second =
		    std::rotr(Words[Index - 2], 17) ^ std::rotr(Words[Index - 2], 19) ^ (Words[Index - 2] >> 10);
		Words[Index] = Words[Index - 16] + First + Words[Index - 7] + Second;
	}
	auto Work = State;
	for (std::size_t Index = 0; Index < 64; ++Index)
	{
		const std::uint32_t Sigma = std::rotr(Work[4], 6) ^ std::rotr(Work[4], 11) ^ std::rotr(Work[4], 25);
		const std::uint32_t Choice = (Work[4] & Work[5]) ^ (~Work[4] & Work[6]);
		const std::uint32_t First = Work[7] + Sigma + Choice + RoundConstants[Index] + Words[Index];
		const std::uint32_t Sum = std::rotr(Work[0], 2) ^ std::rotr(Work[0], 13) ^ std::rotr(Work[0], 22);
		const std::uint32_t Majority = (Work[0] & Work[1]) ^ (Work[0] & Work[2]) ^ (Work[1] & Work[2]);
		for (std::size_t Position = 7; Position > 0; --Position)
		{
			Work[Position] = Work[Position - 1];
		}
		Work[4] += First;
		Work[0] = First + Sum + Majority;
	}
	for (std::size_t Index = 0; Index < State.size(); ++Index)
	{
		State[Index] += Work[Index];
	}
}
} // namespace Mod::BuildTool
