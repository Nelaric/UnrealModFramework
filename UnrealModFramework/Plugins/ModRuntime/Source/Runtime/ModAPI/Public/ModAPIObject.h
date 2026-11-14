// Copyright (c) 2026 Nelaric Contributors
/** @file ModAPIObject.h */
#pragma once

#include "ModAPIResult.h"

namespace Mod::API
{
struct FBindingState;
struct FHostAccess;
struct FNativeAccess;
struct FViewFactory;
/// Specialized by MHT with stable type and calling-table identities.
template <class T> struct TBindingTraits;

/// An opaque, non-owning object binding with a cached typed calling table.
class MODAPI_API FObjectBinding
{
public:
	/** @brief Validates and reads the cached calling table on the game thread.
	 * @param TypeId Expected stable type identity.
	 * @param Signature Expected generated table signature.
	 * @return The table or a recoverable binding failure.
	 */
	TModResult<const void*> ResolveCalls(FName TypeId, const TCHAR* Signature) const;
	/// Gets a read-only binding with a callback-owned lifetime.
	TModResult<FObjectBinding> Borrow(TWeakPtr<const uint8> Lifetime) const;

private:
	friend class FRegistry;
	friend struct FHostAccess;
	friend struct FNativeAccess;
	friend struct FViewFactory;
	TSharedPtr<FBindingState> State;
};

/// Constructs the generated specialization without exposing an object pointer.
struct FViewFactory
{
	/** @brief Constructs a typed view from an already authorized binding.
	 * @tparam T The source object type.
	 * @param Binding An opaque binding established by the host.
	 * @return The corresponding generated view.
	 */
	template <class T> static auto Make(FObjectBinding Binding);
};
} // namespace Mod::API

/// The primary template has no API; MHT supplies closed specializations.
template <class T> class FModRuntimeObject;

template <class T> auto Mod::API::FViewFactory::Make(FObjectBinding Binding)
{
	return FModRuntimeObject<T>(MoveTemp(Binding));
}
