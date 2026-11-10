// Copyright (c) 2026 Nelaric Contributors
/** @file ModAPIHostSubsystem.h */
#pragma once

#include "ModAPINative.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "ModAPIHostSubsystem.generated.h"

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
} // namespace Mod::API

/// Owns the generated API registry for one game instance.
UCLASS(MinimalAPI)
class UModAPIHostSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
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
	 * @param Caller The identity from OpenLocalPlayer.
	 * @return Success or a lifecycle error.
	 */
	MODAPI_API TModResult<void> RevokeCaller(const Mod::API::FCaller& Caller);

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

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
};
