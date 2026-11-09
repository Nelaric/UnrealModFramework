// Copyright (c) 2026 Nelaric Contributors
/** @file ModApiHost.h */
#pragma once

#include "ModApiRegistry.h"

namespace Mod::Api
{
/// Host-only operations used by concrete generated calling bridges.
struct MODAPI_API FHostAccess
{
	/** @brief Enters a synchronous generated call on the game thread.
	 * @param Binding The previously bound target.
	 * @param TypeId Expected source type.
	 * @param Scope Required scope kind.
	 * @param Permissions Required exact permission identifiers.
	 * @param bMutable Whether the source member may mutate its target.
	 * @return The host object or a recoverable validation failure.
	 */
	static TModResult<UObject*> Enter(const FObjectBinding& Binding, FName TypeId, EScope Scope,
	                                  TConstArrayView<FName> Permissions, bool bMutable);
	/** @brief Projects a returned object into an authorized non-owning binding.
	 * @param Source The caller and world of the current invocation.
	 * @param Object The actual returned object.
	 * @param TypeId Expected projected type.
	 * @param Signature Expected generated table signature.
	 * @param bReadOnly Whether mutation through the result is prohibited.
	 * @return An object binding, or a null/type/scope failure.
	 */
	static TModResult<FObjectBinding> ProjectObject(const FObjectBinding& Source, UObject* Object, FName TypeId,
	                                                const TCHAR* Signature, bool bReadOnly);
};
} // namespace Mod::Api
