// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "ModInstance.h"
#include "UMFDevSDKAPI.h"

#include "TestModInstance.generated.h"

class ATestModPreviewPawn;
class USkeletalMesh;

UCLASS(MinimalAPI, Blueprintable)
class UTestModInstance : public UModInstance
{
	GENERATED_BODY()

public:
	UTestModInstance();

	UWorld* GetWorld() const override;

	/** @brief Replaces the local character's mesh with UE's Manny mannequin.
	 * @param PlayerContext The authorized context used when running in a host.
	 * @return Whether the replacement succeeded.
	 */
	UFUNCTION(BlueprintCallable, Category = "Test Mod")
	bool ReplaceCharacterMeshWithManny(const FUMFDevSDKPlayerContext& PlayerContext);

	/// The context supplied by the host for the delayed Blueprint callback.
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Test Mod")
	FUMFDevSDKPlayerContext HostPlayerContext;

protected:
	void OnModLoadCompleted(const FModLoadNotification& Result) override;

private:
	friend class ATestModPreviewPawn;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> ReplacementMesh;
};
