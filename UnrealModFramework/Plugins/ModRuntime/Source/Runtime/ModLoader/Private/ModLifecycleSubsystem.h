// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "ModLoader.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "ModLifecycleSubsystem.generated.h"

class UModInstance;

USTRUCT()
struct FActiveModInstance
{
	GENERATED_BODY()

	FString ModId;

	UPROPERTY(Transient)
	TObjectPtr<UModInstance> Instance;
};

UCLASS()
class UModLifecycleSubsystem final : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	bool ShouldCreateSubsystem(UObject* Outer) const override;
	void Initialize(FSubsystemCollectionBase& Collection) override;
	void Deinitialize() override;
	const FString* FindModId(const UModInstance& Instance) const;

private:
	void SubscribeToLoader();
	void ActivateMountedPackages();
	void Activate(const Mod::Loader::FMountedPackage& Package);
	void Deactivate(const FString& ModId);
	void HandleBeforeUnload(const Mod::Loader::FMountedPackage& Package);
	void HandleLoadCompleted(Mod::Loader::ELoadOrigin Origin, const FString& Path,
	                         TConstArrayView<Mod::Loader::FLoadResult> Results);
	void DispatchResults(Mod::Loader::ELoadOrigin Origin, TConstArrayView<Mod::Loader::FLoadResult> Results);

#if WITH_EDITOR
	void HandlePIEMapReady(UGameInstance* GameInstance);
	FDelegateHandle PIEMapReadyHandle;
	bool bPIE = false;
#endif

	UPROPERTY(Transient)
	TArray<FActiveModInstance> Instances;
	TSet<FString> ActivatedModIds;
	FDelegateHandle CompletionHandle;
	FDelegateHandle BeforeUnloadHandle;
	bool bActive = false;
};
