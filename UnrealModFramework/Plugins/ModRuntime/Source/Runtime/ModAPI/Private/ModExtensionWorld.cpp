// Copyright (c) 2026 Nelaric Contributors
#include "ModExtensionWorld.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "ModAPIHostSubsystem.h"
#include "ModInstance.h"
#include "ModLoader.h"
#include "Templates/UnrealTemplate.h"

DEFINE_LOG_CATEGORY_STATIC(LogModExtensionWorld, Log, All);

using namespace Mod::Extension;
using namespace Mod::Loader;

FModExtensionWorld::FModExtensionWorld(UWorld& InWorld, uint64 InGeneration) : World(&InWorld), Generation(InGeneration)
{
}

bool FModExtensionWorld::HasGrant(const FString& ModId, FName Permission) const
{
	const auto* Grants = Policy.Grants.Find(ModId);
	return Permission.IsNone() || (Grants && Grants->Contains(Permission));
}

bool FModExtensionWorld::Owns(const UModInstance& Instance) const
{
	return Instance.GetWorld() == World.Get() && Instances.Contains(FObjectKey(&Instance));
}

bool FModExtensionWorld::Admit(const FDefinition& Definition) const
{
	const UWorld* Current = World.Get();
	return Current && !Current->bIsTearingDown && (!Definition.bAuthorityOnly || Current->GetNetMode() != NM_Client);
}

bool FModExtensionWorld::RefreshSnapshots(FString& Error)
{
	TArray<FName> Keys;
	Channels.GetKeys(Keys);
	Keys.Sort(FNameLexicalLess());
	for (const FName Id : Keys)
	{
		const auto& Channel = Channels.FindChecked(Id);
		if (!Channel->Build(Error))
		{
			for (const auto& Pending : Channels)
			{
				Pending.Value->Discard();
			}
			bDirty = false;
			return false;
		}
	}
	for (const auto& Entry : Channels)
	{
		Entry.Value->Publish();
	}
	bDirty = false;
	return true;
}

bool FModExtensionWorld::Prepare(FString& Error)
{
	IModLoaderModule& Loader = IModLoaderModule::Get();
	const auto Packages = Loader.GetMountedPackages();
	if (Packages.Num() > 128)
	{
		Error = TEXT("The World mod plan exceeds the mounted-package limit.");
		return false;
	}
	TMap<FString, int32> Selected;
	TArray<FHandler> Order;
	TArray<TSharedPtr<const FMountedPackage>> Leases;
	for (const FMountedPackage& Package : Packages)
	{
		if (!Policy.EnabledMods.IsEmpty() && !Policy.EnabledMods.Contains(Package.ModId))
		{
			continue;
		}
		auto Lease = Loader.PinMountedPackage(Package.ModId);
		if (!Lease)
		{
			Error = TEXT("Package became unavailable: ") + Package.ModId;
			return false;
		}
		Selected.Add(Package.ModId, Order.Num());
		Order.AddDefaulted_GetRef().ModId = Package.ModId;
		Leases.Add(MoveTemp(Lease));
	}
	for (const FLoadResult& Result : Loader.GetStartupResults())
	{
		if (!Result.IsSuccess() && Result.Package.Extensions.bDeclared && !Selected.Contains(Result.Package.ModId) &&
		    (Policy.EnabledMods.IsEmpty() || Policy.EnabledMods.Contains(Result.Package.ModId)))
		{
			Error = TEXT("Could not prepare extension package ") + Result.Package.ModId + TEXT(": ") + Result.Detail;
			return false;
		}
	}
	for (int32 Index = 0; Index < Leases.Num(); ++Index)
	{
		const auto& Package = *Leases[Index];
		for (const FModDependency& Dependency : Package.Extensions.Mods)
		{
			const int32* Provider = Selected.Find(Dependency.ModId);
			if (!Provider || !Dependency.Versions.Contains(Leases[*Provider]->Version))
			{
				if (Dependency.bRequired)
				{
					Error = Package.ModId + TEXT(": incompatible or missing mod ") + Dependency.ModId;
					return false;
				}
				continue;
			}
			Order[Index].Order.After.AddUnique(Dependency.ModId);
		}
		for (const FServiceDependency& Dependency : Package.Extensions.Services)
		{
			if (Selected.Contains(Dependency.Provider))
			{
				Order[Index].Order.After.AddUnique(Dependency.Provider);
			}
			else if (Dependency.bRequired)
			{
				Error = Package.ModId + TEXT(": missing service provider ") + Dependency.Provider;
				return false;
			}
		}
	}
	TArray<int32> Sorted;
	if (!SortExtensions(Order, Sorted, Error))
	{
		return false;
	}
	UWorld* Current = World.Get();
	auto* Game = Current ? Current->GetGameInstance() : nullptr;
	auto* APIHost = Game ? Game->GetSubsystem<UModAPIHostSubsystem>() : nullptr;
	APIRegistry = APIHost ? APIHost->GetRegistry() : nullptr;
	APIHostOwner = APIHost;
	if (APIRegistry)
	{
		const auto Scope = APIHost->FindOrOpenWorldScope(Current);
		if (!Scope.IsSuccess())
		{
			Error = TEXT("Could not open the extension World scope.");
			return false;
		}
		APIScope = Scope.GetValue();
	}
	TArray<FDefinition> Definitions;
	FGeneratedRegistration::DescribeAll(Definitions);
	for (FDefinition& Definition : Definitions)
	{
		if (Definition.Id.IsNone() || Channels.Contains(Definition.Id))
		{
			Error = TEXT("Invalid or duplicate extension ID: ") + Definition.Id.ToString();
			return false;
		}
		const FName Id = Definition.Id;
		if (Definition.Kind == EKind::Hook)
		{
			const FString Prefix = Id.ToString() + TEXT(".");
			Definition.ModifyInputsPermission = FName(Prefix + TEXT("ModifyInputs"));
			Definition.CancelPermission = FName(Prefix + TEXT("Cancel"));
			Definition.ModifyResultPermission = FName(Prefix + TEXT("ModifyResult"));
		}
		Channels.Add(Id, MakeShared<FChannel>(MoveTemp(Definition)));
	}
	for (const auto& Package : Leases)
	{
		for (const auto& Requirement : Package->Extensions.Contracts)
		{
			const auto* Channel = Channels.Find(Requirement.Id);
			if (!Channel || (*Channel)->GetDefinition().Signature != Requirement.Signature)
			{
				Error = Package->ModId + TEXT(": incompatible extension ") + Requirement.Id.ToString();
				return false;
			}
		}
	}
	for (int32 Index : Sorted)
	{
		FMod& Mod = Mods.AddDefaulted_GetRef();
		Mod.Package = MoveTemp(Leases[Index]);
		if (!Mod.Package->Extensions.bDeclared)
		{
			continue;
		}
		for (const FString& Path : Mod.Package->LifecycleClasses)
		{
			UClass* Class = LoadClass<UModInstance>(nullptr, *Path);
			if (!Class || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) ||
			    !Class->GetPathName().Equals(Path, ESearchCase::IgnoreCase) || Phase != EState::Preparing)
			{
				Error = Mod.Package->ModId + TEXT(": invalid lifecycle class ") + Path;
				return false;
			}
			UModInstance* Instance = NewObject<UModInstance>(Game, Class);
			UModInstance::SetExtensionWorld(*Instance, *Current, Mod.Package->ModId);
			Instances.Add(FObjectKey(Instance), Mods.Num() - 1);
			Mod.Instances.Emplace(Instance);
		}
	}
	if (!ResolveContent(Error) || !PreparePatches(Error))
	{
		return false;
	}
	if (Phase != EState::Preparing)
	{
		Error = TEXT("World closed while extension resources were preparing.");
		return false;
	}
	for (const FMod& Mod : Mods)
	{
		for (const auto& Instance : Mod.Instances)
		{
			Instance->RegisterExtensions();
			if (!RegistrationError.IsEmpty())
			{
				Error = RegistrationError;
				return false;
			}
			if (Phase != EState::Preparing)
			{
				Error = TEXT("World closed during extension registration.");
				return false;
			}
		}
	}
	if (!ValidateServices(Error))
	{
		return false;
	}
	bBindingServices = true;
	for (const FMod& Mod : Mods)
	{
		for (const auto& Instance : Mod.Instances)
		{
			Instance->BindExtensions();
			if (Phase != EState::Preparing)
			{
				Error = TEXT("World closed during extension binding.");
				return false;
			}
		}
	}
	bBindingServices = false;
	if (!RegistrationError.IsEmpty())
	{
		Error = RegistrationError;
		return false;
	}
	if (!ValidateServices(Error) || !RefreshSnapshots(Error) || !ApplyPatches(true, Error) ||
	    !ValidateServices(Error) || (bDirty && !RefreshSnapshots(Error)))
	{
		return false;
	}
	if (Phase != EState::Preparing)
	{
		Error = TEXT("World closed before the extension plan could be published.");
		return false;
	}
	return true;
}

bool FModExtensionWorld::ValidateServices(FString& Error) const
{
	for (const FMod& Mod : Mods)
	{
		for (const auto& Need : Mod.Package->Extensions.Services)
		{
			const FServiceEntry* Entry = Services.Find(Need.Id);
			if (Need.bRequired &&
			    (!Entry || !Entry->Owner.IsValid() || !IsValid(Entry->Target.Get()) || Entry->ModId != Need.Provider ||
			     Entry->Definition.TypeId != Need.TypeId || Entry->Definition.Signature != Need.Signature ||
			     !Need.Versions.Contains(Entry->Definition.Version) ||
			     !HasGrant(Mod.Package->ModId, Entry->Definition.Permission)))
			{
				Error =
				    Mod.Package->ModId + TEXT(": incompatible, unauthorized or missing service ") + Need.Id.ToString();
				return false;
			}
		}
	}
	return true;
}

void FModExtensionWorld::FinishDispatch()
{
	check(Depth > 0);
	if (--Depth != 0)
	{
		return;
	}
	if (Phase == EState::Draining)
	{
		Drain();
		return;
	}
	++Depth;
	FlushRemovedAnchors();
	--Depth;
	if (Phase == EState::Draining)
	{
		Drain();
	}
	else if (bDirty && Phase == EState::Ready)
	{
		FString Error;
		if (!RefreshSnapshots(Error))
		{
			UE_LOG(LogModExtensionWorld, Error, TEXT("%s"), *Error);
		}
	}
}

void FModExtensionWorld::Drain()
{
	if (Phase == EState::Closed || Depth || bStopping)
	{
		return;
	}
	TGuardValue<bool> Guard(bStopping, true);
	Phase = EState::Draining;
	bBindingServices = false;
	Channels.Reset();
	BlueprintCalls.Reset();
	RevertPatches();
	Patches.Reset();
	Contents.Reset();
	ContentLeases.Reset();
	for (int32 Index = Mods.Num() - 1; Index >= 0; --Index)
	{
		const FMod& Mod = Mods[Index];
		for (const auto& Instance : Mod.Instances)
		{
			if (Instance)
			{
				Instance->ReleaseExtensions();
			}
			if (APIHostOwner.IsValid() && APIHostOwner->GetRegistry() == APIRegistry && Instance)
			{
				if (auto* Caller = Callers.Find(FObjectKey(Instance.Get())))
				{
					APIRegistry->RevokeCaller(*Caller);
				}
			}
			if (Instance)
			{
				Instances.Remove(FObjectKey(Instance.Get()));
			}
		}
		for (auto Entry = Services.CreateIterator(); Entry; ++Entry)
		{
			if (Entry.Value().ModId == Mod.Package->ModId)
			{
				Entry.RemoveCurrent();
			}
		}
	}
	for (const auto& Entry : NativeCallers)
	{
		Mod::API::FNativeAccess::Close(Entry.Value.Context);
	}
	NativeCallers.Reset();
	Callers.Reset();
	Services.Reset();
	Mods.RemoveAll([this](const FMod& Mod) { return !FailedCompensations.Contains(Mod.Package->ModId); });
	Anchors.Reset();
	RemovedAnchors.Reset();
	Phase = EState::Closed;
}
