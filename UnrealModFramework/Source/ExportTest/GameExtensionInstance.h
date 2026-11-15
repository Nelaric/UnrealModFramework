// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "ModExtensionSession.h"

#include "GameExtensionInstance.generated.h"

struct FActorsInitializedParams;

UCLASS(MinimalAPI)
class UGameExtensionInstance final : public UGameInstance
{
	GENERATED_BODY()

public:
	FString GetModLoginOption();

public:
	virtual void Init() override;
	virtual void Shutdown() override;
	virtual void HandleGameNetControlMessage(UNetConnection* Connection, uint8 MessageByte,
	                                         const FString& Message) override;
	virtual bool DelayPendingNetGameTravel() override;
	virtual bool DelayCompletionOfPendingNetGameTravel() override;

private:
	void PrepareClient(const FActorsInitializedParams& Params);
	void ResetOutgoingPlan();
	Mod::Extension::FModNetworkHandshake Handshake;
	FString ClientPlan;
	FString ClientFailure;
	bool bPlanRequired = false;
	FDelegateHandle ActorsHandle;
};

UCLASS(MinimalAPI)
class UGameExtensionLocalPlayer final : public ULocalPlayer
{
	GENERATED_BODY()

public:
	virtual FString GetGameLoginOptions() const override;
};
