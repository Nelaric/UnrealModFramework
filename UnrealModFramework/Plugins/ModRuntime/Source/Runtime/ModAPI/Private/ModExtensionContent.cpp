// Copyright (c) 2026 Nelaric Contributors
#include "ModExtensionWorld.h"

#include "ModAPIHostSubsystem.h"

#include "HAL/PlatformTime.h"

#include "Algo/AllOf.h"
#include "Components/SceneComponent.h"
#include "Engine/DataAsset.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "ModLoader.h"

DEFINE_LOG_CATEGORY_STATIC(LogModContent, Log, All);

using namespace Mod::Extension;
using namespace Mod::Loader;
using Mod::API::EError;

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

FSoftObjectPath PatchAssetPath(const FWorldPatch& Patch)
{
	switch (Patch.Operation)
	{
	case EWorldPatchOperation::SpawnActor:
		return FSoftObjectPath(Patch.ActorClass.ToString());
	case EWorldPatchOperation::AttachComponent:
		return FSoftObjectPath(Patch.ComponentClass.ToString());
	case EWorldPatchOperation::RegisterData:
		return Patch.DataAsset;
	}
	return {};
}
} // namespace

bool UModExtensionSubsystem::DefineContentSlot(FName Id, FContentSlot Slot)
{
	if (!IsInGameThread() || !State || State->Phase != FModExtensionWorld::EState::New || Id.IsNone() ||
	    !Slot.AssetType || (Slot.bRequired && Slot.bLoadOnDemand) || State->Slots.Contains(Id))
	{
		return false;
	}
	State->Slots.Add(Id, MoveTemp(Slot));
	return true;
}

FResolvedContent UModExtensionSubsystem::ResolveContent(FName Slot, FName Id) const
{
	if (!IsReady() && (!IsInGameThread() || !State || !State->bStagingSavedState ||
	                   State->Phase != FModExtensionWorld::EState::Preparing))
	{
		return {};
	}
	const FContentKey Key(Slot, Id);
	const auto WorldState = State;
	if (!WorldState->PendingContent.Contains(Key))
	{
		return WorldState->LoadingContent.Contains(Key) ? FResolvedContent{} : WorldState->Contents.FindRef(Key);
	}
	auto Result = WorldState->Contents.FindRef(Key);
	WorldState->PendingContent.Remove(Key);
	WorldState->LoadingContent.Add(Key);
	++WorldState->Depth;
	ON_SCOPE_EXIT
	{
		WorldState->LoadingContent.Remove(Key);
		WorldState->FinishDispatch();
	};
	FString Error;
	if (!WorldState->LoadContent(Key, Result, Error) ||
	    (WorldState->Phase != FModExtensionWorld::EState::Ready &&
	     (WorldState->Phase != FModExtensionWorld::EState::Preparing || !WorldState->bStagingSavedState)))
	{
		return {};
	}
	WorldState->Contents.Add(Key, Result);
	return Result;
}

bool FModExtensionWorld::ResolveContent(FString& Error)
{
	TMap<FContentKey, TArray<FCandidate>> Candidates;
	TMap<FContentKey, TSet<FString>> Providers;
	for (const auto& Mod : Mods)
	{
		for (const auto& Entry : Mod.Package->Extensions.Content)
		{
			if (Entry.Action == EContentAction::Provide)
			{
				Providers.FindOrAdd({Entry.Slot, Entry.Id}).Add(Mod.Package->ModId);
			}
		}
	}
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
			const auto* Sources = Providers.Find(Key);
			const bool bHasDependencySource =
			    Slot && Slot->bAllowReplaceProvided && Sources &&
			    Package.Extensions.Mods.ContainsByPredicate(
			        [&](const FModDependency& Need) { return Need.bRequired && Sources->Contains(Need.ModId); });
			const bool bDisabled = Entry.Action == EContentAction::Disable;
			const bool bActionAllowed =
			    Slot && (Entry.Action == EContentAction::Provide ? Slot->bAllowProvide && !bHasDefault
			             : bDisabled                             ? Slot->bAllowDisable
			                         : Slot->bAllowReplace && (bHasDefault || bHasDependencySource));
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
			if (Slots.FindChecked(Key.Key).bLoadOnDemand)
			{
				PendingContent.Add(Key);
			}
			else if (!LoadContent(Key, Result, Error))
			{
				return false;
			}
		}
		if (Phase != EState::Preparing)
		{
			Error = TEXT("The World closed while resolving content.");
			return false;
		}
		Contents.Add(Key, MoveTemp(Result));
	}
	return true;
}

bool FModExtensionWorld::LoadContent(const FContentKey& Key, FResolvedContent& Result, FString& Error)
{
	const auto& Slot = Slots.FindChecked(Key.Key);
	const auto Load = [&Slot](const FSoftObjectPath& Path) -> UObject*
	{
		UObject* Asset = Path.IsNull() ? nullptr : Path.TryLoad();
		return IsValid(Asset) && Asset->IsA(Slot.AssetType) && FSoftObjectPath(Asset) == Path &&
		               (!Slot.ValidateAsset || Slot.ValidateAsset(*Asset))
		           ? Asset
		           : nullptr;
	};
	UObject* Asset = Load(Result.Path);
	if (!Asset && !Slot.bRequired)
	{
		Asset = Load(Slot.Fallback);
		if (Asset)
		{
			Result.Path = Slot.Fallback;
			Result.ModId.Reset();
			Result.bUsedFallback = true;
		}
	}
	if (Asset)
	{
		ContentLeases.Emplace(Asset);
		return true;
	}
	if (Slot.bRequired)
	{
		Error = TEXT("Missing or incompatible asset at ") + DescribeKey(Key) + TEXT(": ") + Result.Path.ToString();
		return false;
	}
	Result.Status = EContentStatus::Missing;
	return true;
}

bool UModExtensionSubsystem::RegisterAnchor(FName Id, ULevel& Level, const FTransform& Transform, FName Permission,
                                            FPatchAnchorRules Rules)
{
	if (!IsInGameThread() || !State || Level.GetWorld() != GetWorld() || Id.IsNone() || Permission.IsNone() ||
	    Transform.ContainsNaN() || State->Anchors.Contains(Id) || State->Depth ||
	    (State->Phase != FModExtensionWorld::EState::New && !IsReady()))
	{
		return false;
	}
	const USceneComponent* Parent = Rules.Attachment.Get();
	if ((Parent && (!Parent->IsRegistered() || !IsValid(Parent->GetOwner()) ||
	                Parent->GetOwner()->GetLevel() != &Level || Parent->GetWorld() != GetWorld())) ||
	    (Rules.DataAssetType && !Rules.DataAssetType->IsChildOf(UDataAsset::StaticClass())))
	{
		return false;
	}
	const auto WorldState = State;
	auto& Anchor = WorldState->Anchors.Add(Id);
	Anchor.Level = &Level;
	Anchor.Transform = Transform;
	Anchor.Permission = Permission;
	Anchor.Generation = ++WorldState->LastAnchor;
	Anchor.Rules = Rules;
	Anchor.DataAssetType.Reset(Rules.DataAssetType);
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
		WorldState->Fail(EError::NotReady, MoveTemp(Error));
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

TOptional<FPatchOrigin> UModExtensionSubsystem::GetPatchOrigin(const UObject& Effect) const
{
	if (!IsInGameThread() || !State || Effect.GetWorld() != GetWorld())
	{
		return {};
	}
	const auto* Applied =
	    State->Applied.FindByPredicate([&Effect](const auto& Record) { return Record.Effect.Get() == &Effect; });
	return Applied ? TOptional<FPatchOrigin>(State->DescribePatch(Applied->PatchIndex, Applied->AnchorGeneration))
	               : TOptional<FPatchOrigin>();
}

FPatchData UModExtensionSubsystem::ResolvePatchData(FName Anchor, FName DataId) const
{
	if (!IsReady())
	{
		return {};
	}
	const auto* Registration = State->PatchData.Find({Anchor, DataId});
	if (!Registration)
	{
		return {};
	}
	const auto& Asset = State->Patches[Registration->Key].Asset;
	return {Cast<UDataAsset>(Asset.Get()), State->DescribePatch(Registration->Key, Registration->Value)};
}

FPatchOrigin FModExtensionWorld::DescribePatch(int32 Index, uint64 AnchorGeneration) const
{
	const FPatch& Patch = Patches[Index];
	return {Patch.ModId, Patch.Definition.Id, Patch.Revision, Patch.Definition.Anchor, Generation, AnchorGeneration};
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
	TSet<FContentKey> DataKeys;
	TArray<FHandler> Graph;
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
			const FContentKey DataKey(Patch.Anchor, Patch.DataId);
			const FSoftObjectPath Path = PatchAssetPath(Patch);
			if ((!Patch.ConflictKey.IsNone() && Conflicts.Contains(Key)) ||
			    (!Patch.DataId.IsNone() && DataKeys.Contains(DataKey)) || !IsCookedAsset(Package, Path))
			{
				Error = Package.ModId + TEXT(": conflicting or invalid Patch ") + Patch.Id.ToString();
				return false;
			}
			if (!Patch.ConflictKey.IsNone())
			{
				Conflicts.Add(Key);
			}
			if (!Patch.DataId.IsNone())
			{
				DataKeys.Add(DataKey);
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
			UObject* Asset = nullptr;
			if (Patch.Operation == EWorldPatchOperation::RegisterData)
			{
				Asset = Cast<UDataAsset>(Patch.DataAsset.TryLoad());
			}
			else
			{
				auto* Class = Patch.Operation == EWorldPatchOperation::SpawnActor
				                  ? Patch.ActorClass.TryLoadClass<AActor>()
				                  : Patch.ComponentClass.TryLoadClass<USceneComponent>();
				if (Class && !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
				{
					Asset = Class;
				}
			}
			if (!IsValid(Asset) || FSoftObjectPath(Asset) != Path)
			{
				Error = Package.ModId + TEXT(": invalid Patch asset ") + Path.ToString();
				return false;
			}
			FPatch& Planned = Patches.AddDefaulted_GetRef();
			Planned.ModId = Package.ModId;
			Planned.Definition = Patch;
			Planned.Revision = Package.ArchiveSha256;
			Planned.Asset.Reset(Asset);
			if (Anchor && !SupportsPatch(Planned, *Anchor))
			{
				Error = Package.ModId + TEXT(": anchor does not accept Patch ") + Patch.Id.ToString();
				return false;
			}
			Planned.bFoundAnchor = Anchor != nullptr;
			Planned.AnchorDeadline = Patch.WaitSeconds > 0 ? FPlatformTime::Seconds() + Patch.WaitSeconds : 0;
			FHandler& Node = Graph.AddDefaulted_GetRef();
			Node.ModId = Package.ModId + TEXT("/") + Patch.Id.ToString().ToLower();
			for (const FName Id : Patch.After)
			{
				Planned.RequiredPatches.Add(Package.ModId + TEXT("/") + Id.ToString().ToLower());
			}
			for (const FName Service : Patch.RequiresServices)
			{
				const auto* Need = Package.Extensions.Services.FindByPredicate([Service](const auto& Entry)
				                                                               { return Entry.Id == Service; });
				const auto* Provider = Need ? Mods.FindByPredicate([Need](const auto& Entry)
				                                                   { return Entry.Package->ModId == Need->Provider; })
				                            : nullptr;
				const auto* Offer = Provider ? Provider->Package->Extensions.Provides.FindByPredicate(
				                                   [Service](const auto& Entry) { return Entry.Id == Service; })
				                             : nullptr;
				if (!Need)
				{
					Error = Node.ModId + TEXT(": undeclared Patch service ") + Service.ToString();
					return false;
				}
				if (Offer && !Offer->Patch.IsNone())
				{
					Planned.RequiredPatches.AddUnique(Need->Provider + TEXT("/") + Offer->Patch.ToString().ToLower());
				}
			}
			if (Planned.RequiredPatches.Contains(Node.ModId))
			{
				Error = Node.ModId + TEXT(": Patch depends on a service it provides.");
				return false;
			}
			Planned.RequiredPatches.Sort();
			Node.Order.After = Planned.RequiredPatches;
		}
	}
	TArray<int32> PlanOrder;
	if (!SortExtensions(Graph, PlanOrder, Error))
	{
		return false;
	}
	TArray<FPatch> Ordered;
	Ordered.Reserve(Patches.Num());
	for (int32 Index : PlanOrder)
	{
		Ordered.Add(MoveTemp(Patches[Index]));
	}
	Patches = MoveTemp(Ordered);
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
		auto& Patch = Patches[Index];
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
		if (!Patch.bFoundAnchor && Patch.AnchorDeadline > 0 && FPlatformTime::Seconds() >= Patch.AnchorDeadline)
		{
			continue;
		}
		Patch.bFoundAnchor = true;
		const auto AnchorGeneration = Anchor->Generation;
		if (IsPatchApplied(Index, AnchorGeneration))
		{
			continue;
		}
		const auto* Package =
		    Mods.FindByPredicate([&Patch](const auto& Mod) { return Mod.Package->ModId == Patch.ModId; });
		check(Package);
		const bool bDependenciesApplied =
		    Algo::AllOf(Patch.Definition.After,
		                [&](FName Id)
		                {
			                return Applied.ContainsByPredicate(
			                    [&](const auto& Record)
			                    {
				                    const auto& Dependency = Patches[Record.PatchIndex];
				                    return Dependency.ModId == Patch.ModId && Dependency.Definition.Id == Id &&
				                           Record.AnchorGeneration == AnchorGeneration && Record.Effect.IsValid();
			                    });
		                }) &&
		    Algo::AllOf(Patch.Definition.RequiresServices,
		                [&](FName Id)
		                {
			                const auto* Need = Package->Package->Extensions.Services.FindByPredicate(
			                    [Id](const auto& Entry) { return Entry.Id == Id; });
			                return Need && IsServiceAvailable(*Need, Patch.ModId);
		                });
		if (Current->GetNetMode() == NM_Client)
		{
			continue;
		}
		if (!HasGrant(Patch.ModId, Anchor->Permission) || !bDependenciesApplied || !SupportsPatch(Patch, *Anchor))
		{
			if (Patch.Definition.bRequired)
			{
				Error = Patch.ModId + TEXT(": Patch authorization or dependency failed for ") +
				        Patch.Definition.Id.ToString();
				return false;
			}
			continue;
		}
		if (!CommitPatch(Index, *Anchor, Error))
		{
			return false;
		}
	}
	return true;
}

bool FModExtensionWorld::SupportsPatch(const FPatch& Patch, const FAnchor& Anchor) const
{
	switch (Patch.Definition.Operation)
	{
	case EWorldPatchOperation::SpawnActor:
		return Anchor.Rules.bAllowSpawnActor;
	case EWorldPatchOperation::AttachComponent:
		if (const auto* Parent = Anchor.Rules.Attachment.Get(); Parent && Parent->IsRegistered())
		{
			const auto* Owner = Parent->GetOwner();
			return IsValid(Owner) && Owner->GetLevel() == Anchor.Level.Get() && !Owner->IsActorBeingDestroyed();
		}
		return false;
	case EWorldPatchOperation::RegisterData:
		return Anchor.DataAssetType && Patch.Asset->IsA(Anchor.DataAssetType.Get());
	}
	return false;
}

bool FModExtensionWorld::CommitPatch(int32 Index, const FAnchor& Anchor, FString& Error)
{
	UWorld* Current = World.Get();
	const FPatch& Patch = Patches[Index];
	const auto& Definition = Patch.Definition;
	const uint64 AnchorGeneration = Anchor.Generation;
	const auto FailEffect = [&]
	{
		Error = FString::Printf(TEXT("FailedCommitted: %s/%s could not activate its World Patch."), *Patch.ModId,
		                        *Definition.Id.ToString());
		Fail(EError::ObjectUnavailable, Error, true);
		return false;
	};
	if (Definition.Operation == EWorldPatchOperation::RegisterData)
	{
		PatchData.Emplace(FContentKey(Definition.Anchor, Definition.DataId),
		                  TPair<int32, uint64>(Index, AnchorGeneration));
		Applied.Add({Index, AnchorGeneration, Patch.Asset.Get(), Patch.RequiredPatches});
		return true;
	}
	bPatchCommitted = true;
	if (Definition.Operation == EWorldPatchOperation::AttachComponent)
	{
		USceneComponent* Parent = Anchor.Rules.Attachment.Get();
		AActor* Owner = Parent->GetOwner();
		const FTransform Relative = Anchor.Transform.GetRelativeTransform(Parent->GetComponentTransform());
		auto* Component =
		    NewObject<USceneComponent>(Owner, CastChecked<UClass>(Patch.Asset.Get()), NAME_None, RF_Transient);
		Applied.Add({Index, AnchorGeneration, Component, Patch.RequiredPatches});
		Owner->AddInstanceComponent(Component);
		Component->SetupAttachment(Parent);
		Component->SetRelativeTransform(Relative);
		Component->RegisterComponent();
		return (IsValid(Component) && Component->IsRegistered() && Component->GetAttachParent() == Parent &&
		        !Component->IsBeingDestroyed() && !Current->bIsTearingDown && Phase != EState::Draining) ||
		       FailEffect();
	}

	FActorSpawnParameters Params;
	Params.OverrideLevel = Anchor.Level.Get();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FTransform Transform = Anchor.Transform;
	AActor* Actor = Current->SpawnActor(CastChecked<UClass>(Patch.Asset.Get()), &Transform, Params);
	if (Actor)
	{
		Applied.Add({Index, AnchorGeneration, Actor, Patch.RequiredPatches});
	}
	if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Current->bIsTearingDown || Phase == EState::Draining)
	{
		return FailEffect();
	}
	if (!RegisterPatchServices(Index, AnchorGeneration, *Actor, Error))
	{
		Fail(EError::NotRegistered, Error, true);
		return false;
	}
	return true;
}

bool FModExtensionWorld::RegisterPatchServices(int32 PatchIndex, uint64 AnchorGeneration, AActor& Actor, FString& Error)
{
	const auto& Patch = Patches[PatchIndex];
	const auto* Mod = Mods.FindByPredicate([&Patch](const auto& Entry) { return Entry.Package->ModId == Patch.ModId; });
	check(Mod);
	for (const auto& Offer : Mod->Package->Extensions.Provides)
	{
		if (Offer.Patch != Patch.Definition.Id)
		{
			continue;
		}
		if (Services.Contains(Offer.Id) || !APIHostOwner.IsValid() || APIHostOwner->GetRegistry() != APIRegistry ||
		    !APIRegistry)
		{
			Error = TEXT("FailedCommitted: unavailable Patch service ") + Offer.Id.ToString();
			return false;
		}
		auto Registered =
		    APIRegistry->RegisterStagedService(Offer.Id, Offer.TypeId, *Offer.Signature, APIScope, &Actor, AsShared());
		if (!Registered.IsSuccess())
		{
			Error = TEXT("FailedCommitted: incompatible Patch service ") + Offer.Id.ToString();
			return false;
		}
		auto& Service = Services.Add(Offer.Id);
		Service.ModId = Patch.ModId;
		Service.Definition = {Offer.Id, Offer.TypeId, Offer.Version, Offer.Signature, Offer.Permission};
		Service.Registration = MoveTemp(Registered.GetValue());
		Service.Target.Reset(&Actor);
		Service.PatchIndex = PatchIndex;
		Service.AnchorGeneration = AnchorGeneration;
	}
	return true;
}

void FModExtensionWorld::RevertPatches(uint64 AnchorGeneration)
{
	TSet<FString> RemovedPatches;
	for (const FAppliedPatch& Record : Applied)
	{
		if (AnchorGeneration && Record.AnchorGeneration != AnchorGeneration &&
		    !Record.DependsOn.ContainsByPredicate([&RemovedPatches](const FString& Key)
		                                          { return RemovedPatches.Contains(Key); }))
		{
			continue;
		}
		const auto& Definition = Patches[Record.PatchIndex];
		RemovedPatches.Add(Definition.ModId + TEXT("/") + Definition.Definition.Id.ToString().ToLower());
	}
	for (int32 Index = Applied.Num() - 1; Index >= 0; --Index)
	{
		const auto& Definition = Patches[Applied[Index].PatchIndex];
		if (!RemovedPatches.Contains(Definition.ModId + TEXT("/") + Definition.Definition.Id.ToString().ToLower()))
		{
			continue;
		}
		const int32 PatchIndex = Applied[Index].PatchIndex;
		const auto Effect = Applied[Index].Effect;
		const uint64 GenerationToRemove = Applied[Index].AnchorGeneration;
		Applied.RemoveAt(Index);
		for (auto Service = Services.CreateIterator(); Service; ++Service)
		{
			if (Service.Value().PatchIndex == PatchIndex && Service.Value().AnchorGeneration == GenerationToRemove)
			{
				Service.RemoveCurrent();
			}
		}
		bool bRemoved = true;
		if (Definition.Definition.Operation == EWorldPatchOperation::RegisterData)
		{
			const FContentKey Key(Definition.Definition.Anchor, Definition.Definition.DataId);
			if (const auto* Entry = PatchData.Find(Key);
			    Entry && Entry->Key == PatchIndex && Entry->Value == GenerationToRemove)
			{
				PatchData.Remove(Key);
			}
		}
		else if (auto* Actor = Cast<AActor>(Effect.Get()); Actor && Actor->GetWorld() == World.Get())
		{
			bRemoved = Actor->IsActorBeingDestroyed() || Actor->Destroy();
		}
		else if (auto* Component = Cast<USceneComponent>(Effect.Get());
		         Component && Component->GetWorld() == World.Get() && !Component->IsBeingDestroyed())
		{
			Component->DestroyComponent();
			bRemoved = !IsValid(Component);
		}
		if (!bRemoved)
		{
			Fail(EError::BindingExpired,
			     FString::Printf(TEXT("FailedCommitted: could not remove %s/%s."), *Definition.ModId,
			                     *Definition.Definition.Id.ToString()),
			     true);
		}
	}
}
