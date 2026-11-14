// Copyright (c) 2026 Nelaric Contributors
#include "ModExtensionWorld.h"

#include "Algo/AllOf.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "ModLoader.h"

DEFINE_LOG_CATEGORY_STATIC(LogModContent, Log, All);

using namespace Mod::Extension;
using namespace Mod::Loader;

namespace
{
struct FCandidate
{
	FString ModId;
	FSoftObjectPath Path;
	int32 Priority = 0;
	bool bDisabled = false;
};

FString DescribeKey(const FContentKey& Key)
{
	return Key.Key.ToString() + TEXT("/") + Key.Value.ToString();
}

FName SlotGrant(FName Slot)
{
	return FName(TEXT("Content.") + Slot.ToString());
}

bool IsCookedAsset(const FMountedPackage& Package, const FSoftObjectPath& Path)
{
	return Package.PackageNames.Contains(Path.GetLongPackageFName());
}

const FCandidate* Choose(TArray<FCandidate>& Candidates, const FString* Override)
{
	Candidates.Sort(
	    [](const FCandidate& Left, const FCandidate& Right)
	    {
		    if (Left.ModId.IsEmpty() != Right.ModId.IsEmpty())
		    {
			    return !Left.ModId.IsEmpty();
		    }
		    return Left.Priority == Right.Priority ? Left.ModId < Right.ModId : Left.Priority > Right.Priority;
	    });
	if (Override)
	{
		return Candidates.FindByPredicate([Override](const FCandidate& Entry) { return Entry.ModId == *Override; });
	}
	if (Candidates.Num() > 1 && !Candidates[1].ModId.IsEmpty() && Candidates[0].Priority == Candidates[1].Priority)
	{
		return nullptr;
	}
	return &Candidates[0];
}
} // namespace

bool UModExtensionSubsystem::DefineContentSlot(FName Id, FContentSlot Slot)
{
	if (!IsInGameThread() || !State || State->Phase != FModExtensionWorld::EState::New || Id.IsNone() ||
	    !Slot.AssetType || State->Slots.Contains(Id))
	{
		return false;
	}
	State->Slots.Add(Id, MoveTemp(Slot));
	return true;
}

FResolvedContent UModExtensionSubsystem::ResolveContent(FName Slot, FName Id) const
{
	return IsReady() ? State->Contents.FindRef({Slot, Id}) : FResolvedContent{};
}

bool FModExtensionWorld::ResolveContent(FString& Error)
{
	TMap<FContentKey, TArray<FCandidate>> Candidates;
	for (const auto& [SlotId, Slot] : Slots)
	{
		for (const auto& [Id, Asset] : Slot.Defaults)
		{
			Candidates.FindOrAdd({SlotId, Id}).Add({{}, Asset});
		}
	}
	for (const auto& Mod : Mods)
	{
		const auto& Package = *Mod.Package;
		for (const FContentCandidate& Entry : Package.Extensions.Content)
		{
			const FContentKey Key(Entry.Slot, Entry.Id);
			const FContentSlot* Slot = Slots.Find(Entry.Slot);
			const bool bHasDefault = Slot && Slot->Defaults.Contains(Entry.Id);
			const bool bDisabled = Entry.Action == EContentAction::Disable;
			const bool bActionAllowed =
			    Slot && (Entry.Action == EContentAction::Provide ? Slot->bAllowProvide && !bHasDefault
			             : bDisabled                             ? Slot->bAllowDisable
			                                                     : Slot->bAllowReplace && bHasDefault);
			if (!bActionAllowed || !HasGrant(Package.ModId, SlotGrant(Entry.Slot)) ||
			    (!bDisabled && !IsCookedAsset(Package, Entry.Asset)))
			{
				Error = Package.ModId + TEXT(": invalid or unauthorized content candidate ") + DescribeKey(Key);
				return false;
			}
			Candidates.FindOrAdd(Key).Add({Package.ModId, Entry.Asset, Entry.Priority, bDisabled});
		}
	}
	for (const auto& [Key, ModId] : Policy.ContentWinners)
	{
		if (!Candidates.Contains(Key))
		{
			Error = TEXT("Content selection names an unknown ID: ") + DescribeKey(Key);
			return false;
		}
	}
	TArray<FContentKey> Keys;
	Candidates.GetKeys(Keys);
	Keys.Sort(
	    [](const FContentKey& Left, const FContentKey& Right)
	    { return Left.Key == Right.Key ? Left.Value.LexicalLess(Right.Value) : Left.Key.LexicalLess(Right.Key); });
	for (const auto& Key : Keys)
	{
		auto& Entries = Candidates.FindChecked(Key);
		const FCandidate* Winner = Choose(Entries, Policy.ContentWinners.Find(Key));
		FResolvedContent Result;
		for (const FCandidate& Entry : Entries)
		{
			Result.Candidates.Add((Entry.ModId.IsEmpty() ? FString(TEXT("<base>")) : Entry.ModId) +
			                      FString::Printf(TEXT(" (%d) "), Entry.Priority) + Entry.Path.ToString());
		}
		if (!Winner)
		{
			Error = TEXT("Content conflict at ") + DescribeKey(Key) + TEXT(": ") +
			        FString::Join(Result.Candidates, TEXT(", "));
			return false;
		}
		Result.ModId = Winner->ModId;
		Result.Path = Winner->Path;
		Result.Status = Winner->bDisabled ? EContentStatus::Disabled : EContentStatus::Available;
		if (!Winner->bDisabled)
		{
			const auto& Slot = Slots.FindChecked(Key.Key);
			UObject* Asset = Winner->Path.TryLoad();
			if (!Asset || !Asset->IsA(Slot.AssetType) || FSoftObjectPath(Asset) != Winner->Path)
			{
				if (Slot.bRequired)
				{
					Error = TEXT("Missing or incompatible asset at ") + DescribeKey(Key) + TEXT(": ") +
					        Winner->Path.ToString();
					return false;
				}
				Result.Status = EContentStatus::Missing;
			}
			else
			{
				ContentLeases.Emplace(Asset);
			}
		}
		Contents.Add(Key, MoveTemp(Result));
	}
	return true;
}

bool UModExtensionSubsystem::RegisterAnchor(FName Id, ULevel& Level, const FTransform& Transform, FName Permission)
{
	if (!IsInGameThread() || !State || Level.GetWorld() != GetWorld() || Id.IsNone() || Permission.IsNone() ||
	    Transform.ContainsNaN() || State->Anchors.Contains(Id) || State->Depth ||
	    (State->Phase != FModExtensionWorld::EState::New && !IsReady()))
	{
		return false;
	}
	const auto WorldState = State;
	WorldState->Anchors.Add(Id, {&Level, Transform, Permission, ++WorldState->LastAnchor});
	if (!IsReady())
	{
		return true;
	}
	++WorldState->Depth;
	ON_SCOPE_EXIT
	{
		WorldState->FinishDispatch();
	};
	FString Error;
	if (!WorldState->ApplyPatches(false, Error))
	{
		UE_LOG(LogModContent, Error, TEXT("%s"), *Error);
		WorldState->Phase = FModExtensionWorld::EState::Draining;
		return false;
	}
	return WorldState->Phase == FModExtensionWorld::EState::Ready;
}

void UModExtensionSubsystem::RemoveAnchor(FName Id, ULevel& Level)
{
	if (!IsInGameThread() || !State || State->Phase == FModExtensionWorld::EState::Closed)
	{
		return;
	}
	const auto WorldState = State;
	WorldState->RemovedAnchors.AddUnique({Id, &Level});
	if (!WorldState->Depth && !WorldState->bStopping)
	{
		++WorldState->Depth;
		WorldState->FinishDispatch();
	}
}

void FModExtensionWorld::FlushRemovedAnchors()
{
	auto Removed = MoveTemp(RemovedAnchors);
	for (const auto& [Id, Level] : Removed)
	{
		const auto* Entry = Anchors.Find(Id);
		if (Entry && Entry->Level == Level)
		{
			const uint64 AnchorGeneration = Entry->Generation;
			Anchors.Remove(Id);
			RevertPatches(AnchorGeneration);
		}
	}
}

bool FModExtensionWorld::PreparePatches(FString& Error)
{
	TSet<TPair<FName, FName>> Conflicts;
	for (const FMod& Mod : Mods)
	{
		const auto& Package = *Mod.Package;
		TArray<FHandler> Order;
		for (const auto& Patch : Package.Extensions.Patches)
		{
			auto& Entry = Order.AddDefaulted_GetRef();
			Entry.ModId = Patch.Id.ToString().ToLower();
			Entry.Key = Patch.Id;
			for (FName Id : Patch.After)
			{
				if (!Package.Extensions.Patches.ContainsByPredicate([Id](const auto& Item) { return Item.Id == Id; }))
				{
					Error = Package.ModId + TEXT(": missing Patch dependency ") + Id.ToString();
					return false;
				}
				Entry.Order.After.Add(Id.ToString().ToLower());
			}
		}
		TArray<int32> Sorted;
		if (!SortExtensions(Order, Sorted, Error))
		{
			Error = Package.ModId + TEXT(": ") + Error;
			return false;
		}
		for (int32 Index : Sorted)
		{
			const auto& Patch = Package.Extensions.Patches[Index];
			const TPair<FName, FName> Key(Patch.Anchor, Patch.ConflictKey);
			if ((!Patch.ConflictKey.IsNone() && Conflicts.Contains(Key)) ||
			    !IsCookedAsset(Package, FSoftObjectPath(Patch.ActorClass.ToString())))
			{
				Error = Package.ModId + TEXT(": conflicting or invalid Patch ") + Patch.Id.ToString();
				return false;
			}
			if (!Patch.ConflictKey.IsNone())
			{
				Conflicts.Add(Key);
			}
			for (const FName Required : Patch.After)
			{
				const auto* Dependency = Package.Extensions.Patches.FindByPredicate([Required](const auto& Item)
				                                                                    { return Item.Id == Required; });
				if (Dependency && Dependency->Anchor != Patch.Anchor)
				{
					Error =
					    Package.ModId + TEXT(": Patch dependencies must use the same anchor: ") + Patch.Id.ToString();
					return false;
				}
			}
			const FAnchor* Anchor = Anchors.Find(Patch.Anchor);
			if ((Patch.bRequired && !Anchor) || (Anchor && !HasGrant(Package.ModId, Anchor->Permission)))
			{
				Error = Package.ModId + TEXT(": missing or unauthorized Patch anchor ") + Patch.Anchor.ToString();
				return false;
			}
			UClass* Class = Patch.ActorClass.TryLoadClass<AActor>();
			if (!Class || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) ||
			    FSoftClassPath(Class) != Patch.ActorClass)
			{
				Error = Package.ModId + TEXT(": invalid Patch actor class ") + Patch.ActorClass.ToString();
				return false;
			}
			Patches.Add({Package.ModId, Patch, TStrongObjectPtr<UClass>(Class)});
		}
	}
	return true;
}

bool FModExtensionWorld::IsPatchApplied(int32 Index, uint64 AnchorGeneration) const
{
	return Applied.ContainsByPredicate(
	    [Index, AnchorGeneration](const auto& Entry)
	    { return Entry.PatchIndex == Index && Entry.AnchorGeneration == AnchorGeneration; });
}

bool FModExtensionWorld::ApplyPatches(bool bInitial, FString& Error)
{
	UWorld* Current = World.Get();
	if (!Current || Current->bIsTearingDown || (Phase != EState::Preparing && Phase != EState::Ready))
	{
		Error = TEXT("World closed before Patch commit.");
		return false;
	}
	for (int32 Index = 0; Index < Patches.Num(); ++Index)
	{
		const auto& Patch = Patches[Index];
		const FAnchor* Anchor = Anchors.Find(Patch.Definition.Anchor);
		if (!Anchor || !Anchor->Level.IsValid())
		{
			if (bInitial && Patch.Definition.bRequired)
			{
				Error = Patch.ModId + TEXT(": required Patch anchor is unavailable.");
				return false;
			}
			continue;
		}
		const auto AnchorGeneration = Anchor->Generation;
		if (IsPatchApplied(Index, AnchorGeneration))
		{
			continue;
		}
		const bool bDependenciesApplied = Algo::AllOf(Patch.Definition.After,
		                                              [&](FName Id)
		                                              {
			                                              return Applied.ContainsByPredicate(
			                                                  [&](const auto& Record)
			                                                  {
				                                                  const auto& Dependency = Patches[Record.PatchIndex];
				                                                  return Dependency.ModId == Patch.ModId &&
				                                                         Dependency.Definition.Id == Id &&
				                                                         Record.Actor.IsValid();
			                                                  });
		                                              });
		if (Current->GetNetMode() == NM_Client)
		{
			continue;
		}
		if (!HasGrant(Patch.ModId, Anchor->Permission) || !bDependenciesApplied)
		{
			if (Patch.Definition.bRequired)
			{
				Error = Patch.ModId + TEXT(": Patch authorization or dependency failed for ") +
				        Patch.Definition.Id.ToString();
				return false;
			}
			continue;
		}
		FActorSpawnParameters Params;
		Params.OverrideLevel = Anchor->Level.Get();
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FTransform Transform = Anchor->Transform;
		AActor* Actor = Current->SpawnActor(Patch.Class.Get(), &Transform, Params);
		if (Actor)
		{
			Applied.Add({Index, AnchorGeneration, Actor});
		}
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Current->bIsTearingDown || Phase == EState::Draining)
		{
			const FString Reason = FString::Printf(TEXT("FailedCommitted: %s/%s could not complete Actor creation."),
			                                       *Patch.ModId, *Patch.Definition.Id.ToString());
			if (Patch.Definition.bRequired || Phase == EState::Draining)
			{
				Error = Reason;
				return false;
			}
			UE_LOG(LogModContent, Warning, TEXT("%s"), *Reason);
		}
	}
	return true;
}

void FModExtensionWorld::RevertPatches(uint64 AnchorGeneration)
{
	for (int32 Index = Applied.Num() - 1; Index >= 0; --Index)
	{
		if (AnchorGeneration && Applied[Index].AnchorGeneration != AnchorGeneration)
		{
			continue;
		}
		const int32 PatchIndex = Applied[Index].PatchIndex;
		const auto Actor = Applied[Index].Actor;
		Applied.RemoveAt(Index);
		if (AActor* Instance = Actor.Get())
		{
			if (Instance->GetWorld() == World.Get() && !Instance->IsActorBeingDestroyed() && !Instance->Destroy())
			{
				const auto& Patch = Patches[PatchIndex];
				UE_LOG(LogModContent, Error, TEXT("FailedCommitted: could not remove Patch actor for %s/%s."),
				       *Patch.ModId, *Patch.Definition.Id.ToString());
				FailedCompensations.AddUnique(Patch.ModId);
				Phase = EState::Draining;
			}
		}
	}
}
