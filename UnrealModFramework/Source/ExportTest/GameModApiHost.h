// Copyright (c) 2026 Nelaric Contributors
/** @file GameModApiHost.h */
#pragma once

#include "ModApiRegistry.h"
#include "ModApiNative.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "GameModApiHost.generated.h"

class ULocalPlayer;
class UWorld;

/// A host-issued caller and its bound local-player service.
struct FGameModApiCaller
{
	/// Revocable caller identity.
	Mod::Api::FCaller Caller;
	/// Authorized service binding, suitable for the generated view factory.
	Mod::Api::FObjectBinding Binding;
	/// Opaque context and scope tokens for the generated C ABI client.
	Mod::Api::FNativeContext Native;
};

/// Composes generated definitions and explicit local-player providers.
UCLASS()
class EXPORTTEST_API UGameModApiHost : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** @brief Opens a service binding with permissions chosen by game code.
	 * @param Player The explicit local player in this game instance.
	 * @param Permissions Grants approved by the host for this caller.
	 * @return A revocable caller and binding, or a preparation error.
	 */
	TModResult<FGameModApiCaller> OpenLocalPlayer(ULocalPlayer* Player, TSet<FName> Permissions);
	/** @brief Revokes one caller and all views issued to it.
	 * @param Caller The identity returned when opening the service.
	 * @return Success or a lifecycle error.
	 */
	TModResult<void> RevokeCaller(const Mod::Api::FCaller& Caller);
	/** @brief Closes the local player's scopes and providers.
	 * @param Player The local player being removed or changing controller.
	 */
	void CloseLocalPlayer(ULocalPlayer* Player);

public:
	void Initialize(FSubsystemCollectionBase& Collection) override;
	void Deinitialize() override;

private:
	struct FPlayerProvider
	{
		TWeakObjectPtr<ULocalPlayer> Player;
		TWeakObjectPtr<UWorld> World;
		Mod::Api::FScope Scope;
		Mod::Api::FRegistration Registration;
	};

	TUniquePtr<Mod::Api::FRegistry> Registry;
	TArray<Mod::Api::FRegistration> Types;
	TMap<TWeakObjectPtr<UWorld>, Mod::Api::FScope> Worlds;
	TArray<FPlayerProvider> Providers;
};
