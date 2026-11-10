// Copyright (c) 2026 Nelaric Contributors
/** @file ModAPIResult.h */
#pragma once

#include "CoreMinimal.h"

namespace Mod::API
{
/// Recoverable failures of generated synchronous API calls.
enum class EError : uint8
{
	/// The operation succeeded.
	None,
	/// The operation requires the game thread.
	WrongThread,
	/// A scope, caller, registry, or registration has been revoked.
	BindingExpired,
	/// A requested type or provider is not registered.
	NotRegistered,
	/// The compiled SDK does not match the registered calling table.
	SignatureMismatch,
	/// An object is absent or no longer usable for gameplay.
	ObjectUnavailable,
	/// An object or binding belongs to a different scope.
	ScopeMismatch,
	/// The host has not granted a required permission.
	PermissionDenied,
	/// A mutable operation was attempted through a read-only binding.
	ReadOnly,
	/// A registration or scope argument is invalid or already in use.
	InvalidArgument,
};
} // namespace Mod::API

/// A synchronous API result containing either a value or a stable error.
template <class T> class TModResult
{
public:
	/** @brief Constructs a successful result.
	 * @param InValue The returned value.
	 * @return A result owning the value.
	 */
	static TModResult Success(T InValue)
	{
		TModResult Result(Mod::API::EError::None);
		Result.Value.Emplace(MoveTemp(InValue));
		return Result;
	}

	/** @brief Constructs a failed result.
	 * @param InError A non-success error.
	 * @return A result containing no value.
	 */
	static TModResult Failure(Mod::API::EError InError)
	{
		check(InError != Mod::API::EError::None);
		return TModResult(InError);
	}

	/** @brief Reports whether a value is available.
	 * @return True for a successful result.
	 */
	bool IsSuccess() const
	{
		return Error == Mod::API::EError::None;
	}

	/** @brief Returns the operation status.
	 * @return None on success or a stable failure.
	 */
	Mod::API::EError GetError() const
	{
		return Error;
	}

	/** @brief Reads a successful value.
	 * @return The owned value; success must have been checked.
	 */
	const T& GetValue() const
	{
		check(IsSuccess());
		return Value.GetValue();
	}

	/** @brief Accesses a successful value.
	 * @return The owned value; success must have been checked.
	 */
	T& GetValue()
	{
		check(IsSuccess());
		return Value.GetValue();
	}

private:
	explicit TModResult(Mod::API::EError InError) : Error(InError)
	{
	}
	Mod::API::EError Error;
	TOptional<T> Value;
};

/// A successful operation without a value, or a stable failure.
template <> class TModResult<void>
{
public:
	/** @brief Constructs a successful result.
	 * @return A successful status.
	 */
	static TModResult Success()
	{
		return TModResult(Mod::API::EError::None);
	}

	/** @brief Constructs a failed result.
	 * @param InError A non-success error.
	 * @return A failed status.
	 */
	static TModResult Failure(Mod::API::EError InError)
	{
		check(InError != Mod::API::EError::None);
		return TModResult(InError);
	}

	/** @brief Reports success.
	 * @return True if the operation succeeded.
	 */
	bool IsSuccess() const
	{
		return Error == Mod::API::EError::None;
	}

	/** @brief Returns the operation status.
	 * @return None on success or a stable failure.
	 */
	Mod::API::EError GetError() const
	{
		return Error;
	}

private:
	explicit TModResult(Mod::API::EError InError) : Error(InError)
	{
	}
	Mod::API::EError Error;
};
