// Copyright (c) 2026 Nelaric Contributors
/** @file GameCharacter.h */
#pragma once

#include "GameFramework/Character.h"
#include "ModExportAnnotations.h"

#include "GameCharacter.generated.h"

/// A host character exposing a bounded health modification API.
UCLASS()
class EXPORTTEST_API AGameCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	/** @brief Adds finite positive health, clamped to the character's maximum.
	 * @param Amount Requested increase; other values leave health unchanged.
	 * @return The resulting health.
	 */
	MOD_EXPORT_API(ExportId = "Game.Character.AddHealth", ExportVersion = "1.0.0", Scope = World, Thread = GameThread,
	               FailureRule = ReturnError, Permissions = (WorldWrite))
	float AddHealth(float Amount);

private:
	UPROPERTY(VisibleInstanceOnly, Category = "Health")
	float Health = 100.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Health", meta = (ClampMin = "0.0"))
	float MaxHealth = 100.0f;
};
