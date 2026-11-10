// Copyright (c) 2026 Nelaric Contributors
/** @file ModAPIRegistry.h */
#pragma once

#include "ModAPIObject.h"

class ULocalPlayer;
class UModAPIHostSubsystem;
class UWorld;

namespace Mod::API
{
struct FRegistryState;
struct FScopeState;
struct FCallerState;
struct FRegistrationState;

/// Supported scope kinds for the synchronous UObject backend.
enum class EScope : uint8
{
	/// An object belonging to a live world.
	World,
	/// A provider belonging to a particular local player and world.
	Player,
};

/// A generated definition for registration and compatibility checks.
struct FAPIDescriptor
{
	/// Stable export ID.
	const TCHAR* ExportId;
	/// Declared export version.
	const TCHAR* ExportVersion;
	/// Normalized generated signature.
	const TCHAR* Signature;
};

/// One generated object type and its immutable calling table.
struct FTypeDescriptor
{
	/// Stable source type identity.
	FName TypeId;
	/// Actual host class.
	UClass* Class = nullptr;
	/// Required object scope.
	EScope Scope = EScope::World;
	/// Generated calling table signature.
	FString Signature;
	/// Module-owned table; valid until registration is revoked.
	const void* Calls = nullptr;
	/// Definitions owned by this type.
	TArray<FAPIDescriptor> APIs;
	/// Permits non-actor objects with no world in a world-bound view.
	bool bAllowWorldless = false;
	/// Preserves read-only authority when this full view is projected.
	bool bFullView = false;
	/// Resolves this player-scoped service from the chosen local player.
	bool bLocalPlayerSubsystem = false;
};

/// A revocable registration. The owner must release it before unloading code.
class MODAPI_API FRegistration
{
public:
	/// Creates an empty registration.
	FRegistration() = default;
	/// Revokes the registration.
	~FRegistration();
	/** @brief Transfers registration ownership.
	 * @param Other The previous owner.
	 */
	FRegistration(FRegistration&& Other) noexcept;
	/** @brief Transfers registration ownership and revokes the previous value.
	 * @param Other The previous owner.
	 * @return This registration.
	 */
	FRegistration& operator=(FRegistration&& Other) noexcept;
	/// Revokes all bindings derived from this registration on the game thread.
	void Reset();

private:
	FRegistration(const FRegistration&) = delete;
	FRegistration& operator=(const FRegistration&) = delete;

	friend class FRegistry;
	friend class FGeneratedHostRegistration;
	TSharedPtr<FRegistrationState> State;
};

/// A host-issued scope; a player scope also references its world scope.
class FScope
{
private:
	friend class FRegistry;
	TSharedPtr<FScopeState> State;
};

/// A host-issued caller identity and permission grant.
class FCaller
{
private:
	friend class FRegistry;
	friend struct FNativeAccess;
	TSharedPtr<FCallerState> State;
};

/// Owns a session's generated definitions, providers, scopes, and callers.
class MODAPI_API FRegistry
{
public:
	/// Creates a registry on the game thread.
	FRegistry();
	/// Revokes all derived bindings on the game thread.
	~FRegistry();
	/** @brief Registers one generated type and its concrete calling table.
	 * @param Descriptor The immutable module-owned definition.
	 * @return A revocation token, or a registration error.
	 */
	TModResult<FRegistration> RegisterType(FTypeDescriptor Descriptor);
	/** @brief Opens a scope for a live world.
	 * @param World The world owned by the host.
	 * @return A scope, or an argument error.
	 */
	TModResult<FScope> OpenWorldScope(UWorld* World);
	/** @brief Opens a local player scope in a particular world.
	 * @param Player The explicit local player.
	 * @param WorldScope The parent world scope.
	 * @return A scope, or a scope error.
	 */
	TModResult<FScope> OpenPlayerScope(ULocalPlayer* Player, const FScope& WorldScope);
	/** @brief Closes a scope and invalidates its bindings.
	 * @param Scope A scope issued by this registry.
	 * @return Success or a caller/thread error.
	 */
	TModResult<void> CloseScope(const FScope& Scope);
	/** @brief Issues a caller with explicit host-granted permissions.
	 * @param Scope The caller's world or player scope.
	 * @param Permissions The exact permission identifiers granted by the host.
	 * @return A caller, or a scope error.
	 */
	TModResult<FCaller> CreateCaller(const FScope& Scope, TSet<FName> Permissions);
	/** @brief Revokes a caller and all views issued to it.
	 * @param Caller A caller issued by this registry.
	 * @return Success or a caller/thread error.
	 */
	TModResult<void> RevokeCaller(const FCaller& Caller);
	/** @brief Registers an explicit service instance in a scope.
	 * @param TypeId The generated source type.
	 * @param Scope The provider's scope.
	 * @param Instance A non-owned instance managed by the game.
	 * @return A registration token, or a compatibility/scope error.
	 */
	TModResult<FRegistration> RegisterProvider(FName TypeId, const FScope& Scope, UObject* Instance);
	/** @brief Binds a registered provider once for a caller.
	 * @param TypeId The expected source type.
	 * @param Signature The compiled generated table signature.
	 * @param Caller The host-issued caller.
	 * @return An opaque binding, or a compatibility/provider error.
	 */
	TModResult<FObjectBinding> BindProvider(FName TypeId, const TCHAR* Signature, const FCaller& Caller);

private:
	FRegistry(const FRegistry&) = delete;
	FRegistry& operator=(const FRegistry&) = delete;

	friend struct FNativeAccess;
	friend class ::UModAPIHostSubsystem;
	TModResult<FRegistration> RegisterLocalPlayerProvider(FName TypeId, const FScope& Scope);
	TSharedPtr<FRegistryState> State;
};
/** @brief Binds a generated service view once for a host-issued caller.
 * @tparam T The actual source service type.
 * @param Registry The session registry.
 * @param Caller The host-issued identity and grants.
 * @return A typed service view or a binding error.
 */
template <class T> auto BindView(FRegistry& Registry, const FCaller& Caller)
{
	using FView = FModRuntimeObject<T>;
	auto Binding = Registry.BindProvider(TBindingTraits<T>::TypeId(), TBindingTraits<T>::Signature, Caller);
	if (!Binding.IsSuccess())
	{
		return TModResult<FView>::Failure(Binding.GetError());
	}
	return TModResult<FView>::Success(FViewFactory::Make<T>(MoveTemp(Binding.GetValue())));
}
} // namespace Mod::API
