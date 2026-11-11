// Copyright (c) 2026 Nelaric Contributors
/** @file ModInstance.h */
#pragma once

#include "Engine/GameInstance.h"
#include "UObject/Object.h"

#include "ModInstance.generated.h"

/// Blueprint payload for one finished Mod load attempt.
USTRUCT(BlueprintType)
struct MODLOADER_API FModLoadNotification
{
	GENERATED_BODY()

	/// The identity read from the archive, when available.
	UPROPERTY(BlueprintReadOnly, Category = "Mod|Lifecycle")
	FString ModId;

	/// The version read from the archive, when available.
	UPROPERTY(BlueprintReadOnly, Category = "Mod|Lifecycle")
	FString Version;

	/// Name of the archive or of the directory that could not be scanned.
	UPROPERTY(BlueprintReadOnly, Category = "Mod|Lifecycle")
	FString InputName;

	/// Whether the mount succeeded or was already present.
	UPROPERTY(BlueprintReadOnly, Category = "Mod|Lifecycle")
	bool bSucceeded = false;

	/// Whether the exact archive was already present before this operation.
	UPROPERTY(BlueprintReadOnly, Category = "Mod|Lifecycle")
	bool bAlreadyMounted = false;

	/// Whether the result came from the configured startup scan.
	UPROPERTY(BlueprintReadOnly, Category = "Mod|Lifecycle")
	bool bStartup = false;

	/// Stable ModLoader error code, zero on success.
	UPROPERTY(BlueprintReadOnly, Category = "Mod|Lifecycle")
	int32 ErrorCode = 0;

	/// Failure reason supplied by ModLoader.
	UPROPERTY(BlueprintReadOnly, Category = "Mod|Lifecycle")
	FString Reason;
};

/// A game-thread Mod entry point owned by one game instance.
UCLASS(Abstract, BlueprintType, Blueprintable, meta = (DontUseGenericSpawnObject))
class MODLOADER_API UModInstance : public UObject
{
	GENERATED_BODY()

public:
	/// Returns the world of the owning game instance.
	UWorld* GetWorld() const override
	{
		const UGameInstance* GameInstance = GetTypedOuter<UGameInstance>();
		return GameInstance ? GameInstance->GetWorld() : nullptr;
	}

protected:
	/** @brief Receives the result of a load operation on the game thread.
	 * @param Result The completed startup or
	 * manual load result.
	 * @details Call Super::OnModLoadCompleted to dispatch the Blueprint event.
	 */
	virtual void OnModLoadCompleted(const FModLoadNotification& Result)
	{
		ReceiveModLoadCompleted(Result);
	}

	/** @brief Blueprint event for a completed Mod load.
	 * @param Result The same load result passed to the C++
	 * entry point.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Mod|Lifecycle", meta = (DisplayName = "On Mod Load Completed"))
	void ReceiveModLoadCompleted(const FModLoadNotification& Result);

	/** @brief Releases game state and references before this Mod is unloaded.
	 * @details Runs once per instance on
	 * the game thread while the Mod's content
	 * is still mounted. Call Super::OnModBeforeUnload to dispatch the
	 * event.
	 */
	virtual void OnModBeforeUnload()
	{
		ReceiveModBeforeUnload();
	}

	/// Blueprint event dispatched before this Mod's content is unloaded.
	UFUNCTION(BlueprintImplementableEvent, Category = "Mod|Lifecycle", meta = (DisplayName = "On Mod Before Unload"))
	void ReceiveModBeforeUnload();

private:
	friend class UModLifecycleSubsystem;
};
