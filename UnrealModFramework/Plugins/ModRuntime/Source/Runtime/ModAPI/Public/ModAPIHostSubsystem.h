// Copyright (c) 2026 Nelaric Contributors
/** @file ModAPIHostSubsystem.h */
#pragma once

#include "ModAPINative.h"
#include "Delegates/Delegate.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "ModAPIHostSubsystem.generated.h"

class UModInstance;

namespace Mod::API
{
/// An authorized caller and its generated player service binding.
struct FLocalPlayerCaller
{
	/// Revocable caller identity.
	FCaller Caller;
	/// Binding suitable for the generated view factory.
	FObjectBinding Binding;
	/// Context and scope tokens for a native client.
	FNativeContext Native;
};

/// A world-scoped caller and the native tokens for one authorized provider.
struct FWorldCaller
{
	/// Revocable identity owned by the host.
	FCaller Caller;
	/// Context and scope tokens owned by the caller.
	FNativeContext Native;
	/// Object token for the selected provider.
	uint64 Object = 0;
};

/// Selects the world provider and exact permissions for an authorized mod.
struct FWorldAPIGrant
{
	/// World containing the registered provider.
	UWorld* World = nullptr;
	/// Permission identifiers approved by the host.
	TSet<FName> Permissions;
};
} // namespace Mod::API

/// Owns the generated API registry for one game instance.
UCLASS(MinimalAPI)
class UModAPIHostSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** @brief Registers a live world provider on the game thread.
	 * @param TypeId Generated type to expose.
	 * @param Instance Game-owned instance, ready to receive calls.
	 * @return A registration to retain until the provider is removed.
	 */
	MODAPI_API TModResult<Mod::API::FRegistration> RegisterWorldProvider(FName TypeId, UObject* Instance);

	/** @brief Issues a caller and provider token on the game thread.
	 * @param World The active world of the caller.
	 * @param TypeId The registered provider type.
	 * @param Signature The generated calling table signature.
	 * @param Permissions Exact permissions granted by the host.
	 * @return Caller and tokens, or a binding error.
	 */
	MODAPI_API TModResult<Mod::API::FWorldCaller> OpenWorldCaller(UWorld* World, FName TypeId, const TCHAR* Signature,
	                                                              TSet<FName> Permissions);

	/// Handles a game-thread request for a mounted mod's native API caller.
	DECLARE_DELEGATE_RetVal_TwoParams(TModResult<Mod::API::FWorldAPIGrant>, FAuthorizeWorldCaller, const UModInstance&,
	                                  FName);

	/** @brief Sets the Mod authorization policy on the game thread.
	 * @param Policy The delegate that selects a world and permissions.
	 */
	MODAPI_API void SetWorldCallerPolicy(FAuthorizeWorldCaller Policy);

	/** @brief Binds a generated local-player service.
	 * @param Player The local player in this game instance.
	 * @param TypeId The generated type identity.
	 * @param Signature The generated table signature.
	 * @param Permissions The host-approved permissions.
	 * @return The caller, binding and native tokens, or an error.
	 */
	MODAPI_API TModResult<Mod::API::FLocalPlayerCaller>
	OpenLocalPlayer(ULocalPlayer* Player, FName TypeId, const TCHAR* Signature, TSet<FName> Permissions);

	/** @brief Binds a typed local-player service.
	 * @tparam T The exported subsystem type.
	 * @param Player The local player in this game instance.
	 * @param Permissions The host-approved permissions.
	 * @return The caller, binding and native tokens, or an error.
	 */
	template <class T>
	TModResult<Mod::API::FLocalPlayerCaller> OpenLocalPlayer(ULocalPlayer* Player, TSet<FName> Permissions)
	{
		using FTraits = Mod::API::TBindingTraits<T>;
		return OpenLocalPlayer(Player, FTraits::TypeId(), FTraits::Signature, MoveTemp(Permissions));
	}

	/** @brief Revokes one caller and all of its bindings.
	 * @param Caller The identity issued by this subsystem.
	 * @return Success or a lifecycle error.
	 */
	MODAPI_API TModResult<void> RevokeCaller(const Mod::API::FCaller& Caller);

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	static void* ResolveHostQuery(const char* Module, const char* Symbol);
	TModResult<Mod::API::FWorldCaller> AuthorizeWorldCaller(UModInstance& Mod, FName TypeId, const TCHAR* Signature);
	void RevokeModCallers(UModInstance& Mod);
	MODAPI_API Mod::API::FRegistry* GetRegistry() const
	{
		return Registry.Get();
	}
	MODAPI_API TModResult<Mod::API::FScope> FindOrOpenWorldScope(UWorld* World);

private:
	struct FPlayerScope
	{
		TWeakObjectPtr<UWorld> World;
		Mod::API::FScope Scope;
		FDelegateHandle ControllerChanged;
		TMap<FName, Mod::API::FRegistration> Providers;
	};

	TModResult<FPlayerScope*> OpenPlayerScope(ULocalPlayer* Player, UWorld* World);
	void CloseLocalPlayer(ULocalPlayer* Player);
	void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void ReleasePlayerScope(const TWeakObjectPtr<ULocalPlayer>& Player, const FPlayerScope& Entry);

	TUniquePtr<Mod::API::FRegistry> Registry;
	TArray<Mod::API::FRegistration> Types;
	TMap<TWeakObjectPtr<UWorld>, Mod::API::FScope> Worlds;
	TMap<TWeakObjectPtr<ULocalPlayer>, FPlayerScope> Players;
	TSharedPtr<const FAuthorizeWorldCaller> WorldCallerPolicy;
	TMap<TWeakObjectPtr<UModInstance>, TMap<FName, Mod::API::FWorldCaller>> ModCallers;
};
