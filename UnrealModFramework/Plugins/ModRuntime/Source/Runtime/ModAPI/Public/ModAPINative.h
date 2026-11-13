// Copyright (c) 2026 Nelaric Contributors
/** @file ModAPINative.h */
#pragma once

#include "ModAPIRegistry.h"

namespace Mod::API
{
/// Process-local tokens issued by the host for an authorized caller.
struct FNativeContext
{
	/// Calling identity; zero is never a valid context.
	uint64 Context = 0;
	/// The caller's scope; this token cannot be used by another context.
	uint64 Scope = 0;
};

/// Host-only token access for generated fixed-signature C ABI bridges.
struct MODAPI_API FNativeAccess
{
	/** @brief Issues opaque tokens for a host-approved caller.
	 * @param Registry The caller's registry.
	 * @param Caller The host-issued caller and permission grant.
	 * @return Tokens or a thread/lifetime failure.
	 */
	static TModResult<FNativeContext> Open(FRegistry& Registry, const FCaller& Caller);
	/** @brief Releases a native context and every object token it owns.
	 * @param Context A token issued by Open.
	 * @return Success or a thread/lifetime failure.
	 */
	static TModResult<void> Close(uint64 Context);
	/** @brief Resolves a scope/provider or object token for a generated call.
	 * @param Context The calling identity.
	 * @param Target The caller's scope or an object issued to this context.
	 * @param TypeId The exact generated source type.
	 * @param Signature The expected generated calling-table signature.
	 * @return A checked binding without exposing a raw object address.
	 */
	static TModResult<FObjectBinding> Resolve(uint64 Context, uint64 Target, FName TypeId, const TCHAR* Signature);
	/** @brief Resolves an object token on the game thread.
	 * @param Context The calling identity.
	 * @param Target An object token issued to this context.
	 * @param TypeId The exact generated source type.
	 * @param Signature The expected generated calling-table signature.
	 * @return The checked object binding or an authorization error.
	 */
	static TModResult<FObjectBinding> ResolveObject(uint64 Context, uint64 Target, FName TypeId,
	                                                const TCHAR* Signature);
	/** @brief Publishes an authorized object as a context-owned token.
	 * @param Context The identity receiving the result.
	 * @param Binding The generated and validated object projection.
	 * @return A nonzero token or a lifetime/scope failure.
	 */
	static TModResult<uint64> Publish(uint64 Context, FObjectBinding Binding);
	/** @brief Releases an object token without affecting the actual UObject.
	 * @param Context The identity owning the token.
	 * @param Object The object token to release.
	 * @return Success or a thread/lifetime failure.
	 */
	static TModResult<void> Release(uint64 Context, uint64 Object);
};
} // namespace Mod::API
