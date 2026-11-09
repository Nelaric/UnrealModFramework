// Copyright (c) 2026 Nelaric Contributors
#include "GameLocalPlayerApi.h"

#include "Engine/LocalPlayer.h"
#include "GameCharacter.h"
#include "GameModApiHost.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"

AGameCharacter* UGameLocalPlayerApi::GetLocalCharacter() const
{
	const ULocalPlayer* Player = GetLocalPlayer();
	const UWorld* World = Player ? Player->GetWorld() : nullptr;
	const APlayerController* Controller = Player && World ? Player->GetPlayerController(World) : nullptr;
	return Controller ? Cast<AGameCharacter>(Controller->GetPawn()) : nullptr;
}

void UGameLocalPlayerApi::Deinitialize()
{
	if (ULocalPlayer* Player = GetLocalPlayer())
	{
		if (UGameInstance* Game = Player->GetGameInstance())
		{
			if (UGameModApiHost* Host = Game->GetSubsystem<UGameModApiHost>())
			{
				Host->CloseLocalPlayer(Player);
			}
		}
	}
	Super::Deinitialize();
}

void UGameLocalPlayerApi::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	if (ULocalPlayer* Player = GetLocalPlayer())
	{
		if (UGameInstance* Game = Player->GetGameInstance())
		{
			if (UGameModApiHost* Host = Game->GetSubsystem<UGameModApiHost>())
			{
				Host->CloseLocalPlayer(Player);
			}
		}
	}
	Super::PlayerControllerChanged(NewPlayerController);
}
