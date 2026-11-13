// Copyright (c) 2026 Nelaric Contributors
#include "ModLifecycleSubsystem.h"

#include "Engine/LatentActionManager.h"
#include "Engine/World.h"
#include "HAL/PlatformProperties.h"
#include "Misc/Paths.h"
#include "ModInstance.h"
#include "Modules/ModuleManager.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_EDITOR
#include "Engine/Engine.h"
#include "ModLoaderPIE.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogModLifecycle, Log, All);

using namespace Mod::Loader;

#if WITH_EDITOR
namespace
{
bool IsPIEGameInstance(const UObject* Outer)
{
	const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
	const FWorldContext* Context = GameInstance ? GameInstance->GetWorldContext() : nullptr;
	return Context && Context->WorldType == EWorldType::PIE;
}
} // namespace
#endif

bool UModLifecycleSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (IsRunningCommandlet() || !Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
#if WITH_EDITOR
	if (IsPIEGameInstance(Outer))
	{
		return Private::IsPIEActive();
	}
#endif
	return FPlatformProperties::RequiresCookedData();
}

void UModLifecycleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bActive = true;
#if WITH_EDITOR
	bPIE = IsPIEGameInstance(GetGameInstance());
	if (bPIE)
	{
		PIEMapReadyHandle = FWorldDelegates::OnPIEMapReady.AddUObject(this, &UModLifecycleSubsystem::HandlePIEMapReady);
		return;
	}
#endif
	SubscribeToLoader();
}

void UModLifecycleSubsystem::SubscribeToLoader()
{
	IModLoaderModule& Loader = IModLoaderModule::Get();
	CompletionHandle = Loader.OnLoadCompleted().AddUObject(this, &UModLifecycleSubsystem::HandleLoadCompleted);
	BeforeUnloadHandle = Loader.OnBeforeUnload().AddUObject(this, &UModLifecycleSubsystem::HandleBeforeUnload);
	ActivateMountedPackages();
}

void UModLifecycleSubsystem::ActivateMountedPackages()
{
	for (const FMountedPackage& Package : IModLoaderModule::Get().GetMountedPackages())
	{
		Activate(Package);
	}
}

void UModLifecycleSubsystem::Deinitialize()
{
	bActive = false;
#if WITH_EDITOR
	FWorldDelegates::OnPIEMapReady.Remove(PIEMapReadyHandle);
#endif
	if (IModLoaderModule* Loader = FModuleManager::GetModulePtr<IModLoaderModule>(TEXT("ModLoader")))
	{
		Loader->OnLoadCompleted().Remove(CompletionHandle);
		Loader->OnBeforeUnload().Remove(BeforeUnloadHandle);
	}
	while (!Instances.IsEmpty())
	{
		const FString ModId = Instances.Last().ModId;
		Deactivate(ModId);
	}
	ActivatedModIds.Reset();
	Super::Deinitialize();
}

#if WITH_EDITOR
void UModLifecycleSubsystem::HandlePIEMapReady(UGameInstance* GameInstance)
{
	if (GameInstance != GetGameInstance() || !bActive || !Private::IsPIEActive())
	{
		return;
	}
	FWorldDelegates::OnPIEMapReady.Remove(PIEMapReadyHandle);
	SubscribeToLoader();
	const TArray<FLoadResult> Results = Private::GetPIEStartupResults();
	DispatchResults(ELoadOrigin::Startup, Results);
}
#endif

void UModLifecycleSubsystem::Activate(const FMountedPackage& Package)
{
	if (!bActive || Package.LifecycleClasses.IsEmpty() || ActivatedModIds.Contains(Package.ModId))
	{
		return;
	}
	ActivatedModIds.Add(Package.ModId);
	for (const FString& Path : Package.LifecycleClasses)
	{
		if (!bActive)
		{
			return;
		}
		UClass* Class = LoadClass<UModInstance>(nullptr, *Path);
		if (!bActive)
		{
			return;
		}
		if (!Class || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) ||
		    !Class->GetPathName().Equals(Path, ESearchCase::IgnoreCase))
		{
			UE_LOG(LogModLifecycle, Warning, TEXT("Could not activate mod '%s' lifecycle class '%s'."), *Package.ModId,
			       *Path);
			continue;
		}
		Instances.Add({Package.ModId, NewObject<UModInstance>(GetGameInstance(), Class)});
	}
}

const FString* UModLifecycleSubsystem::FindModId(const UModInstance& Instance) const
{
	const auto* Entry =
	    Instances.FindByPredicate([&Instance](const auto& Active) { return Active.Instance == &Instance; });
	return Entry ? &Entry->ModId : nullptr;
}

void UModLifecycleSubsystem::HandleBeforeUnload(const FMountedPackage& Package)
{
	Deactivate(Package.ModId);
}

void UModLifecycleSubsystem::Deactivate(const FString& ModId)
{
	ActivatedModIds.Remove(ModId);
	TArray<TStrongObjectPtr<UModInstance>> Unloading;
	Instances.RemoveAll(
	    [&ModId, &Unloading](const FActiveModInstance& Entry)
	    {
		    if (Entry.ModId != ModId)
		    {
			    return false;
		    }
		    Unloading.Emplace(Entry.Instance.Get());
		    return true;
	    });
	for (const TStrongObjectPtr<UModInstance>& Instance : Unloading)
	{
		Instance->RevokeWorldAPIs();
	}
	for (const TStrongObjectPtr<UModInstance>& Instance : Unloading)
	{
		Instance->OnModBeforeUnload();
		if (UWorld* World = Instance->GetWorld())
		{
			World->GetLatentActionManager().RemoveActionsForObject(Instance.Get());
			World->GetTimerManager().ClearAllTimersForObject(Instance.Get());
		}
	}
}

void UModLifecycleSubsystem::HandleLoadCompleted(ELoadOrigin Origin, const FString&,
                                                 TConstArrayView<FLoadResult> Results)
{
#if WITH_EDITOR
	if (bPIE && !Private::IsCurrentPIECompletion())
	{
		return;
	}
#endif
	DispatchResults(Origin, Results);
}

void UModLifecycleSubsystem::DispatchResults(ELoadOrigin Origin, TConstArrayView<FLoadResult> Results)
{
	ActivateMountedPackages();
	for (const FLoadResult& Result : Results)
	{
		if (!bActive || Instances.IsEmpty())
		{
			return;
		}
		FModLoadNotification Notification;
		Notification.ModId = Result.Package.ModId;
		Notification.Version = Result.Package.Version;
		Notification.InputName = FPaths::GetCleanFilename(Result.Path);
		Notification.bSucceeded = Result.IsSuccess();
		Notification.bAlreadyMounted = Result.bAlreadyMounted;
		Notification.bStartup = Origin == ELoadOrigin::Startup;
		Notification.ErrorCode = static_cast<int32>(Result.Error);
		Notification.Reason = Result.Detail;
		for (int32 Index = 0; bActive && Index < Instances.Num(); ++Index)
		{
			Instances[Index].Instance->OnModLoadCompleted(Notification);
		}
	}
}
