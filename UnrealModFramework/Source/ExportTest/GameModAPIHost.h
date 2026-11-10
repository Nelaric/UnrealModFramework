// Copyright (c) 2026 Nelaric Contributors
/** @file GameModAPIHost.h */
#pragma once

#include "ModAPIRegistry.h"
#include "ModAPINative.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "GameModAPIHost.generated.h"

class ULocalPlayer;
class UWorld;

/// A host-issued caller and its bound local-player service.
struct FGameModAPICaller
{
	/// Revocable caller identity.
	Mod::API::FCaller Caller;
	/// Authorized service binding, suitable for the generated view factory.
	Mod::API::FObjectBinding Binding;
	/// Opaque context and scope tokens for the generated C ABI client.
	Mod::API::FNativeContext Native;
};

/// Composes generated definitions and explicit local-player providers.
UCLASS()
class EXPORTTEST_API UGameModAPIHost : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** @brief Opens a service binding with permissions chosen by game code.
	 * @param Player The explicit local player in this game instance.
	 * @param Permissions Grants approved by the host for this caller.
	 * @return A revocable caller and binding, or a preparation error.
	 */
	TModResult<FGameModAPICaller> OpenLocalPlayer(ULocalPlayer* Player, TSet<FName> Permissions);
	/** @brief Revokes one caller and all views issued to it.
	 * @param Caller The identity returned when opening the service.
	 * @return Success or a lifecycle error.
	 */
	TModResult<void> RevokeCaller(const Mod::API::FCaller& Caller);
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
		Mod::API::FScope Scope;
		Mod::API::FRegistration Registration;
	};

	TUniquePtr<Mod::API::FRegistry> Registry;
	TArray<Mod::API::FRegistration> Types;
	TMap<TWeakObjectPtr<UWorld>, Mod::API::FScope> Worlds;
	TArray<FPlayerProvider> Providers;
};
