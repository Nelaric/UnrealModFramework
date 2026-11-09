// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "ModLoader.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "ModLifecycleSubsystem.generated.h"

class UModInstance;

UCLASS()
class UModLifecycleSubsystem final : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	bool ShouldCreateSubsystem(UObject* Outer) const override;
	void Initialize(FSubsystemCollectionBase& Collection) override;
	void Deinitialize() override;

private:
	void SubscribeToLoader();
	void Activate(const Mod::Loader::FMountedPackage& Package);
	void HandleLoadCompleted(Mod::Loader::ELoadOrigin Origin, const FString& Path,
	                         TConstArrayView<Mod::Loader::FLoadResult> Results);
	void DispatchResults(Mod::Loader::ELoadOrigin Origin, TConstArrayView<Mod::Loader::FLoadResult> Results);

#if WITH_EDITOR
	void HandlePIEMapReady(UGameInstance* GameInstance);
	FDelegateHandle PIEMapReadyHandle;
	bool bPIE = false;
#endif

	UPROPERTY(Transient)
	TArray<TObjectPtr<UModInstance>> Instances;
	TSet<FString> ActivatedModIds;
	FDelegateHandle CompletionHandle;
	bool bActive = false;
};
