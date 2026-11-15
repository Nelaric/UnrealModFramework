// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "GameFramework/GameModeBase.h"
#include "ModExtensionSave.h"
#include "Templates/Function.h"

#include "GameExtensionMode.generated.h"

bool PrepareGameExtensions(UWorld& World, const FString& ExpectedPlan, FString& Error,
                           TFunction<bool(FString&)> ValidatePreparation = {},
                           TFunction<bool(FString&)> StageSave = {});

UCLASS(MinimalAPI)
class AGameExtensionMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AGameExtensionMode();
	virtual void StartPlay() override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
	                      FString& ErrorMessage) override;
	virtual APlayerController* Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal,
	                                 const FString& Options, const FUniqueNetIdRepl& UniqueId,
	                                 FString& ErrorMessage) override;
	virtual void GameWelcomePlayer(UNetConnection* Connection, FString& RedirectURL) override;
	bool HandleModControlMessage(UNetConnection& Connection, uint8 Message, const FString& Value);
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	UFUNCTION(Exec)
	void ModWriteSave(const FString& Slot);
	UFUNCTION(Exec)
	void ModLoadSave(const FString& Slot);
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

private:
	bool RestorePendingSave(const Mod::Extension::FModSaveSnapshot& Original, FString& Error);
	void RestorePlayer(APlayerController& Player);
	FString RequestedSave;
	TOptional<float> SavedHealth;
	Mod::Extension::FModSaveSnapshot LoadedSave;
	TArray<TWeakObjectPtr<APlayerController>> WaitingPlayers;
	TSet<TWeakObjectPtr<UNetConnection>> AcknowledgedPeers;
};
