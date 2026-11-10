// Copyright (c) 2026 Nelaric Contributors
#include "ErrorCodePolicy.h"

#include "Common/Errors/ErrorCodes.h"
#include "ModSourceTool/src/Errors/CatalogText.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

void ErrorCodePolicy::ValidateLegacyCodes(const Json& Members, const Json& Counter)
{
	if (!Counter.is_number_integer())
	{
		throw std::runtime_error("Catalog nextBusinessCode must be an integer.");
	}
	const auto NextCode = Counter.get<std::int64_t>();
	if (NextCode < FirstBusinessCode || NextCode > LastBusinessCode + 1)
	{
		throw std::runtime_error("Catalog nextBusinessCode is outside the business code range.");
	}
	std::set<std::int64_t> BusinessCodes;
	if (!Members.is_object())
	{
		throw std::runtime_error("Catalog members must be an object keyed by member ID.");
	}
	for (const auto& [MemberId, Member] : Members.items())
	{
		const auto& Reasons = Member.at("reasons");
		if (!Reasons.is_object())
		{
			throw std::runtime_error("Catalog reasons must be an object for " + MemberId);
		}
		for (const auto& [Key, Reason] : Reasons.items())
		{
			if (Key.empty() || !Reason.is_object())
			{
				throw std::runtime_error("Invalid catalog reason in " + MemberId);
			}
			if (!Reason.contains("code"))
			{
				continue;
			}
			const auto& Code = Reason.at("code");
			if (!Code.is_number_integer())
			{
				throw std::runtime_error("Expected an integer error code for " + MemberId + '/' + Key);
			}
			const auto Value = Code.get<std::int64_t>();
			if (Key == "WrongThread" && Value == WrongThreadCode)
			{
				continue;
			}
			if (Key == "WrongThread" || Value < FirstBusinessCode || Value >= NextCode ||
			    !BusinessCodes.insert(Value).second)
			{
				throw std::runtime_error("Invalid or duplicate error code for " + MemberId + '/' + Key);
			}
		}
	}
}

std::uint32_t ErrorCodePolicy::ErrorType(std::string_view Type)
{
	if (Type.size() != 3 || Type == "000" || Type.find_first_not_of("0123456789ABCDEF") != Type.npos)
	{
		throw std::runtime_error("Expected a three-digit hexadecimal error type: " + std::string(Type));
	}
	std::uint32_t Value = 0;
	std::from_chars(Type.data(), Type.data() + Type.size(), Value, 16);
	return Value;
}

const Tools::PublicStatuses::PublicStatus* ErrorCodePolicy::FindFrameworkStatus(std::string_view Key)
{
	const auto End = std::end(Tools::PublicStatuses::FrameworkStatuses);
	const auto Found = std::find_if(std::begin(Tools::PublicStatuses::FrameworkStatuses), End,
	                                [&](const auto& Status) { return Key == Status.Key; });
	return Found == End ? nullptr : Found;
}

void ErrorCodePolicy::ValidateStructuredCodes(const Json& Members, const Json& ClassifiedReasons, const Json& Counters)
{
	if (!Members.is_object() || !Counters.is_object())
	{
		throw std::runtime_error("The catalog requires members and nextDetailByType objects.");
	}
	for (const auto& [Type, Next] : Counters.items())
	{
		ErrorType(Type);
		if (!Next.is_number_integer() || Next.get<std::uint64_t>() == 0 ||
		    Next.get<std::uint64_t>() > Tools::ErrorCodes::ErrorDetailMask + 1)
		{
			throw std::runtime_error("Invalid next detail for error type " + Type);
		}
	}
	std::set<std::uint32_t> BusinessCodes;
	std::set<std::string> Macros;
	for (const auto& Status : Tools::PublicStatuses::FrameworkStatuses)
	{
		Macros.insert(Status.Macro);
	}
	for (const auto& Status : Tools::PublicStatuses::AdapterStatuses)
	{
		Macros.insert(Status.Macro);
	}
	const auto ValidateReasons = [&](const Json& Reasons, const std::string& Scope)
	{
		if (!Reasons.is_object())
		{
			throw std::runtime_error("Catalog reasons must be an object for " + Scope);
		}
		for (const auto& [Key, Reason] : Reasons.items())
		{
			const auto Identity = Scope + '/' + Key;
			const auto Macro = CatalogText::Text(Reason, "macro");
			const auto Code = Reason.find("code");
			if (Code != Reason.end() && !Code->is_number_integer())
			{
				throw std::runtime_error("Expected an integer error code for " + Identity);
			}
			if (const auto* Status = FindFrameworkStatus(Key))
			{
				if (Status->Code == 0 || Macro != Status->Macro || (Code != Reason.end() && *Code != Status->Code))
				{
					throw std::runtime_error("Invalid framework error for " + Identity);
				}
				continue;
			}
			if (Key.empty() || !Tools::ErrorCodes::IsErrorMacro(Macro) || !Macros.insert(Macro).second)
			{
				throw std::runtime_error("Invalid or duplicate error macro for " + Identity);
			}
			const auto Type = CatalogText::Text(Reason, "type");
			const auto Base =
			    Tools::ErrorCodes::EncodeErrorCode(Tools::ErrorCodes::EErrorDomain::Business, ErrorType(Type));
			if (Code != Reason.end())
			{
				const auto Value = Code->get<std::uint64_t>();
				if (Value <= Base || Value > Base + Tools::ErrorCodes::ErrorDetailMask ||
				    Value - Base >= Counters.value(Type, 1u) ||
				    !BusinessCodes.insert(static_cast<std::uint32_t>(Value)).second)
				{
					throw std::runtime_error("Invalid or duplicate business error code for " + Identity);
				}
			}
		}
	};
	ValidateReasons(ClassifiedReasons, "classifiedReasons");
	for (const auto& [MemberId, Member] : Members.items())
	{
		ValidateReasons(Member.at("reasons"), MemberId);
	}
}

std::uint32_t ErrorCodePolicy::AllocateCode(Json& Counters, const Json& Reason, const std::string& Key,
                                            bool bStructured)
{
	if (bStructured)
	{
		if (const auto* Status = FindFrameworkStatus(Key))
		{
			return Status->Code;
		}
		const auto Type = CatalogText::Text(Reason, "type");
		const auto Detail = Counters.value(Type, 1u);
		if (Detail > Tools::ErrorCodes::ErrorDetailMask)
		{
			throw std::runtime_error("The error detail range is exhausted for type " + Type);
		}
		Counters[Type] = Detail + 1;
		return Tools::ErrorCodes::EncodeErrorCode(Tools::ErrorCodes::EErrorDomain::Business, ErrorType(Type), Detail);
	}
	if (Key == "WrongThread")
	{
		return WrongThreadCode;
	}
	const auto NextCode = Counters.get<std::int64_t>();
	if (NextCode > LastBusinessCode)
	{
		throw std::runtime_error("The business error code range is exhausted.");
	}
	Counters = NextCode + 1;
	return static_cast<std::uint32_t>(NextCode);
}
} // namespace Mod::SourceTool
