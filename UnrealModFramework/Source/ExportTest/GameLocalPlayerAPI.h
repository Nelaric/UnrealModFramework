// Copyright (c) 2026 Nelaric Contributors
/** @file GameLocalPlayerAPI.h */
#pragma once

#include "ModExportMacros.h"
#include "Subsystems/LocalPlayerSubsystem.h"

#include "GameLocalPlayerAPI.generated.h"

class AGameCharacter;

/// Resolves the current character of one explicitly bound local player.
UCLASS(MinimalAPI, meta = (ModTypeId = "game.local_player_api"))
class UGameLocalPlayerAPI : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	/** @brief Reads this local player's currently controlled game character.
	 * @return The character, or null while
	 * no matching pawn is available.
	 */
	MOD_EXPORT_API(Thread = GameThread, FailureRule = ReturnError, Permissions = (WorldRead))
	EXPORTTEST_API AGameCharacter* GetLocalCharacter() const;
};
