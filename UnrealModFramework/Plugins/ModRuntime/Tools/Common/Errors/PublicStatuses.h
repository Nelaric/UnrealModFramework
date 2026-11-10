// Copyright (c) 2026 Nelaric Contributors
/** @file PublicStatuses.h */
#pragma once

#include "Common/Errors/ErrorCodes.h"

#include <cstdint>

namespace Mod::Tools
{
class PublicStatuses
{
public:
	struct PublicStatus
	{
		const char* Key;
		const char* Macro;
		std::uint32_t LegacyCode;
		std::uint32_t Code;
	};

	static constexpr PublicStatus FrameworkStatuses[] = {
	    {"None", "MOD_ERROR_NONE", 0, 0},
	    {"WrongThread", "MOD_ERROR_WRONGTHREAD", 1,
	     ErrorCodes::EncodeErrorCode(ErrorCodes::EErrorDomain::Framework, 0x401)},
	    {"BindingExpired", "MOD_ERROR_BINDINGEXPIRED", 2,
	     ErrorCodes::EncodeErrorCode(ErrorCodes::EErrorDomain::Framework, 0x303)},
	    {"NotRegistered", "MOD_ERROR_NOTREGISTERED", 3,
	     ErrorCodes::EncodeErrorCode(ErrorCodes::EErrorDomain::Framework, 0x301)},
	    {"SignatureMismatch", "MOD_ERROR_SIGNATUREMISMATCH", 4,
	     ErrorCodes::EncodeErrorCode(ErrorCodes::EErrorDomain::Framework, 0x603)},
	    {"ObjectUnavailable", "MOD_ERROR_OBJECTUNAVAILABLE", 5,
	     ErrorCodes::EncodeErrorCode(ErrorCodes::EErrorDomain::Framework, 0x302)},
	    {"ScopeMismatch", "MOD_ERROR_SCOPEMISMATCH", 6,
	     ErrorCodes::EncodeErrorCode(ErrorCodes::EErrorDomain::Framework, 0x304)},
	    {"PermissionDenied", "MOD_ERROR_PERMISSIONDENIED", 7,
	     ErrorCodes::EncodeErrorCode(ErrorCodes::EErrorDomain::Framework, 0x501)},
	    {"ReadOnly", "MOD_ERROR_READONLY", 8, ErrorCodes::EncodeErrorCode(ErrorCodes::EErrorDomain::Framework, 0x502)},
	    {"InvalidArgument", "MOD_ERROR_INVALIDARGUMENT", 9,
	     ErrorCodes::EncodeErrorCode(ErrorCodes::EErrorDomain::Framework, 0x101)},
	};

	static constexpr PublicStatus AdapterStatuses[] = {
	    {"NotConnected", "MOD_ERROR_NOTCONNECTED", 65536,
	     ErrorCodes::EncodeErrorCode(ErrorCodes::EErrorDomain::Adapter, 0x601)},
	    {"ABIMismatch", "MOD_ERROR_ABIMISMATCH", 65537,
	     ErrorCodes::EncodeErrorCode(ErrorCodes::EErrorDomain::Adapter, 0x602)},
	    {"InternalError", "MOD_ERROR_INTERNALERROR", 65538,
	     ErrorCodes::EncodeErrorCode(ErrorCodes::EErrorDomain::Adapter, 0x701)},
	};
};
} // namespace Mod::Tools
