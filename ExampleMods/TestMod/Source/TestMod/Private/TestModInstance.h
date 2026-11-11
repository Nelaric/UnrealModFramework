// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "ModInstance.h"
#include "UMFDevSDKAPI.h"

#include "TestModInstance.generated.h"

class ATestModPreviewPawn;
class USkeletalMesh;
class UTestModShooterComponent;

UCLASS(MinimalAPI, Blueprintable)
class UTestModInstance : public UModInstance
{
	GENERATED_BODY()

public:
	UTestModInstance();

	UWorld* GetWorld() const override;

	/** @brief Replaces the local character's mesh with UE's Manny mannequin.
	 * @param PlayerContext The authorized

	 * * context used when running in a host.
	 * @return Whether the replacement succeeded.
	 */
	UFUNCTION(BlueprintCallable, Category = "Test Mod")
	bool ReplaceCharacterMeshWithManny(const FUMFDevSDKPlayerContext& PlayerContext);

	/// The context supplied by the host for the Blueprint callback.
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Test Mod")
	FUMFDevSDKPlayerContext HostPlayerContext;

protected:
	void OnModLoadCompleted(const FModLoadNotification& Result) override;
	void OnModBeforeUnload() override;

private:
	friend class ATestModPreviewPawn;

	static bool HasStandalonePreviewContent();
	static FString ContentPath(const TCHAR* RelativePath);

	void UpdateShooter(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void RemoveShooter();

	UPROPERTY()
	TObjectPtr<USkeletalMesh> PreviewReplacementMesh;

	TWeakObjectPtr<UTestModShooterComponent> Shooter;
	FDelegateHandle WorldTickHandle;
};
