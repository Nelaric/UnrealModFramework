// Copyright (c) 2026 Nelaric Contributors
#include "GameLocalPlayerAPI.h"

#include "Engine/LocalPlayer.h"
#include "GameCharacter.h"
#include "GameModAPIHost.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"

AGameCharacter* UGameLocalPlayerAPI::GetLocalCharacter() const
{
	const ULocalPlayer* Player = GetLocalPlayer();
	const UWorld* World = Player ? Player->GetWorld() : nullptr;
	const APlayerController* Controller = Player && World ? Player->GetPlayerController(World) : nullptr;
	return Controller ? Cast<AGameCharacter>(Controller->GetPawn()) : nullptr;
}

void UGameLocalPlayerAPI::Deinitialize()
{
	if (ULocalPlayer* Player = GetLocalPlayer())
	{
		if (UGameInstance* Game = Player->GetGameInstance())
		{
			if (UGameModAPIHost* Host = Game->GetSubsystem<UGameModAPIHost>())
			{
				Host->CloseLocalPlayer(Player);
			}
		}
	}
	Super::Deinitialize();
}

void UGameLocalPlayerAPI::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	if (ULocalPlayer* Player = GetLocalPlayer())
	{
		if (UGameInstance* Game = Player->GetGameInstance())
		{
			if (UGameModAPIHost* Host = Game->GetSubsystem<UGameModAPIHost>())
			{
				Host->CloseLocalPlayer(Player);
			}
		}
	}
	Super::PlayerControllerChanged(NewPlayerController);
}
