// Copyright (c) 2026 Nelaric Contributors
#include "Hash.h"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <stdexcept>

namespace Mod::HeaderTool
{
namespace
{
constexpr std::array<std::uint32_t, 64> RoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

class FSha256
{
public:
	/// Streams exact bytes into the digest state.
	void Update(std::string_view Bytes)
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

	/// Finalizes this state and returns the canonical digest.
	std::string Finish()
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

private:
	void Transform()
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

	std::array<std::uint32_t, 8> State = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
	                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	std::array<unsigned char, 64> Block{};
	std::size_t Used = 0;
	std::uint64_t ByteCount = 0;
};
} // namespace

std::string HashBytes(std::string_view Bytes)
{
	FSha256 Hash;
	Hash.Update(Bytes);
	return Hash.Finish();
}

std::string HashFile(const fs::path& Path)
{
	if (!fs::is_regular_file(Path))
	{
		throw std::runtime_error("Cannot hash a missing or non-regular file: " + PathToUtf8(Path));
	}
	std::ifstream Stream(Path, std::ios::binary);
	if (!Stream)
	{
		throw std::runtime_error("Cannot open hash input: " + PathToUtf8(Path));
	}
	FSha256 Hash;
	std::array<char, 65536> Buffer{};
	while (Stream)
	{
		Stream.read(Buffer.data(), static_cast<std::streamsize>(Buffer.size()));
		Hash.Update(std::string_view(Buffer.data(), static_cast<std::size_t>(Stream.gcount())));
	}
	if (!Stream.eof())
	{
		throw std::runtime_error("Cannot read hash input: " + PathToUtf8(Path));
	}
	return Hash.Finish();
}

void RequireDigest(std::string_view Digest, std::string_view Field)
{
	if (Digest.size() != 64 || Digest.find_first_not_of("0123456789abcdef") != std::string_view::npos)
	{
		throw std::runtime_error(std::string(Field) + " must be a lowercase SHA-256 digest.");
	}
}
} // namespace Mod::HeaderTool
