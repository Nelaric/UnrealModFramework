// Copyright (c) 2026 Nelaric Contributors
/** @file ErrorCodes.h */
#pragma once

#include <cstdint>
#include <string_view>

namespace Mod::Tools
{
class ErrorCodes
{
public:
	static constexpr int StructuredErrorCodeVersion = 2;
	static constexpr std::uint32_t ErrorDetailMask = 0xFFFF;

	enum class EErrorDomain : std::uint32_t
	{
		Framework = 1,
		Adapter = 2,
		Business = 3,
	};

	static constexpr std::uint32_t EncodeErrorCode(EErrorDomain Domain, std::uint32_t Type, std::uint32_t Detail = 0)
	{
		return (static_cast<std::uint32_t>(Domain) << 28) | (Type << 16) | Detail;
	}

	static constexpr bool IsErrorMacro(std::string_view Macro)
	{
		constexpr std::string_view Prefix = "MOD_ERROR_";
		return Macro.starts_with(Prefix) && Macro.size() > Prefix.size() &&
		       Macro.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_", Prefix.size()) == Macro.npos;
	}
};
} // namespace Mod::Tools
