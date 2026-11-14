// Copyright (c) 2026 Nelaric Contributors
/** @file GameCharacter.h */
#pragma once

#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "ModExportMacros.h"
#include "ModExtension.h"

#include "GameCharacter.generated.h"

/// A host character exposing bounded health and appearance changes.
UCLASS(MinimalAPI)
class AGameCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	/** @brief Adds finite positive health, clamped to the character's maximum.
	 * @param Amount Requested increase;
	 * other values leave health unchanged.
	 * @return The resulting health.
	 */
	MOD_EXPORT_API(Scope = World, Thread = GameThread, FailureRule = ReturnError, Permissions = (WorldWrite))
	EXPORTTEST_API float AddHealth(float Amount);

	/** @brief Sets the character's skeletal mesh on the game thread.
	 * @param NewMesh A loaded mesh with a skeleton
	 * that is compatible with the
	 * active Animation Blueprint, if one is configured.
	 * @return Whether the
	 * character accepted the mesh.
	 */
	MOD_EXPORT_API(Scope = World, Thread = GameThread, FailureRule = ReturnError, Permissions = (WorldWrite))
	UFUNCTION(BlueprintCallable, Category = "Character|Appearance")
	EXPORTTEST_API bool SetMesh(USkeletalMesh* NewMesh);

	/** @brief Restores the skeletal mesh saved before the first replacement.
	 * @return Whether called on the game
	 * thread with a valid mesh component.
	 */
	MOD_EXPORT_API(Scope = World, Thread = GameThread, FailureRule = ReturnError, Permissions = (WorldWrite))
	UFUNCTION(BlueprintCallable, Category = "Character|Appearance")
	EXPORTTEST_API bool RestoreMesh();

public:
	MOD_EXPORT_HOOK(ExportId = "hook/game.character/change-health", Permission = "Health.Change",
	                AllowedStages = (Pre, Post), AllowedOperations = (ModifyInputs, Cancel, ModifyResult))
	UFUNCTION(BlueprintCallable, Category = "Character|Health")
	EXPORTTEST_API float ChangeHealth(float Delta);

	MOD_EXPORT_EVENT(ExportId = "event/game.character/health-changed", SourceScope = World, PublishRule = Immediate,
	                 Permission = "Health.Observe")
	EXPORTTEST_API void PublishHealthChanged(float CurrentHealth);

protected:
	void BeginPlay() override;

private:
	float ApplyHealth(float Delta);
	Mod::Extension::FEndpoint HealthHook;
	Mod::Extension::FEndpoint HealthEvent;

	UPROPERTY(VisibleInstanceOnly, Category = "Health")
	float Health = 100.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Health", meta = (ClampMin = "0.0"))
	float MaxHealth = 100.0f;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> OriginalMesh;

	bool bHasOriginalMesh = false;
};
