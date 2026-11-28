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

	// Published source classifications retain their assigned values on every target.
	static constexpr PublicStatus SourceStatuses[] = {
	    {"InvalidMeshNull", "MOD_ERROR_INVALIDMESH_NULL", 0, 0x32010001},
	    {"InvalidMeshGarbage", "MOD_ERROR_INVALIDMESH_GARBAGE", 0, 0x32010002},
	    {"MeshHasNoSkeleton", "MOD_ERROR_MESH_SKELETON_MISSING", 0, 0x32030001},
	    {"CurrentMeshHasNoSkeleton", "MOD_ERROR_CURRENT_MESH_SKELETON_MISSING", 0, 0x32030002},
	    {"SkeletonMismatch", "MOD_ERROR_SKELETON_MISMATCH", 0, 0x32040001},
	    {"MeshComponentUnavailable", "MOD_ERROR_MESH_COMPONENT_UNAVAILABLE", 0, 0x33020001},
	    {"MeshComponentGarbage", "MOD_ERROR_MESH_COMPONENT_GARBAGE", 0, 0x33020002},
	    {"MeshComponentNull", "MOD_ERROR_MESH_COMPONENT_NULL", 0, 0x33020003},
	    {"DeclaredFailure", "MOD_ERROR_DECLARED_FAILURE", 0,
	     ErrorCodes::EncodeErrorCode(ErrorCodes::EErrorDomain::Adapter, 0x801, 1)},
	    {"NativeException", "MOD_ERROR_NATIVE_EXCEPTION", 0,
	     ErrorCodes::EncodeErrorCode(ErrorCodes::EErrorDomain::Adapter, 0x802, 1)},
	};
};
} // namespace Mod::Tools
