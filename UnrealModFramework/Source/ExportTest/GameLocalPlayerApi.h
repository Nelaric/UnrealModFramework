// Copyright (c) 2026 Nelaric Contributors
/** @file GameLocalPlayerApi.h */
#pragma once

#include "ModExportAnnotations.h"
#include "Subsystems/LocalPlayerSubsystem.h"

#include "GameLocalPlayerApi.generated.h"

class AGameCharacter;

/// Resolves the current character of one explicitly bound local player.
UCLASS()
class EXPORTTEST_API UGameLocalPlayerApi : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	/** @brief Reads this local player's currently controlled game character.
	 * @return The character, or null while no matching pawn is available.
	 */
	MOD_EXPORT_API(ExportId = "Game.Player.GetLocalCharacter", ExportVersion = "1.0.0", Scope = Player,
	               Thread = GameThread, FailureRule = ReturnError, Permissions = (WorldRead))
	AGameCharacter* GetLocalCharacter() const;

public:
	void Deinitialize() override;
	void PlayerControllerChanged(APlayerController* NewPlayerController) override;
};
