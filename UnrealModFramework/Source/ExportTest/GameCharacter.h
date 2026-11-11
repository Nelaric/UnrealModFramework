// Copyright (c) 2026 Nelaric Contributors
/** @file GameCharacter.h */
#pragma once

#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "ModExportMacros.h"

#include "GameCharacter.generated.h"

/// A host character exposing bounded health and appearance changes.
UCLASS()
class EXPORTTEST_API AGameCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	/** @brief Adds finite positive health, clamped to the character's maximum.
	 * @param Amount Requested increase;
	 * other values leave health unchanged.
	 * @return The resulting health.
	 */
	MOD_EXPORT_API(Scope = World, Thread = GameThread, FailureRule = ReturnError, Permissions = (WorldWrite))
	float AddHealth(float Amount);

	/** @brief Sets the character's skeletal mesh on the game thread.
	 * @param NewMesh A loaded mesh with a skeleton
	 * that is compatible with the
	 * active Animation Blueprint, if one is configured.
	 * @return Whether the
	 * character accepted the mesh.
	 */
	MOD_EXPORT_API(Scope = World, Thread = GameThread, FailureRule = ReturnError, Permissions = (WorldWrite))
	UFUNCTION(BlueprintCallable, Category = "Character|Appearance")
	bool SetMesh(USkeletalMesh* NewMesh);

	/** @brief Restores the skeletal mesh saved before the first replacement.
	 * @return Whether called on the game
	 * thread with a valid mesh component.
	 */
	MOD_EXPORT_API(Scope = World, Thread = GameThread, FailureRule = ReturnError, Permissions = (WorldWrite))
	UFUNCTION(BlueprintCallable, Category = "Character|Appearance")
	bool RestoreMesh();

private:
	UPROPERTY(VisibleInstanceOnly, Category = "Health")
	float Health = 100.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Health", meta = (ClampMin = "0.0"))
	float MaxHealth = 100.0f;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> OriginalMesh;

	bool bHasOriginalMesh = false;
};
