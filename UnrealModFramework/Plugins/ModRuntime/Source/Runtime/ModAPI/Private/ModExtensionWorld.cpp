// Copyright (c) 2026 Nelaric Contributors
#include "ModExtensionWorld.h"

#include "Serialization/JsonWriter.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "ModAPIHostSubsystem.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "ModInstance.h"
#include "ModLoader.h"
#include "ModNativeExtension.h"
#include "Templates/UnrealTemplate.h"

DEFINE_LOG_CATEGORY_STATIC(LogModExtensionWorld, Log, All);

using namespace Mod::Extension;
using namespace Mod::Loader;
using Mod::API::EError;

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

bool FModExtensionWorld::SelectPackages(UModAPIHostSubsystem& Host, FString& Error)
{
	IModLoaderModule& Loader = IModLoaderModule::Get();
	if (Loader.IsRestartRequired())
	{
		Error = TEXT("Restart the process before preparing another World.");
		return false;
	}
	auto Selection = Policy.Selection;
	if (!Selection.Enabled && !Policy.EnabledMods.IsEmpty())
	{
		Selection.Enabled = Policy.EnabledMods;
	}
	if (!Host.ExtensionSession)
	{
		const auto Packages = Loader.GetMountedPackages();
		if (Packages.Num() > 128)
		{
			Error = TEXT("The session exceeds the mounted-package limit.");
			return false;
		}
		TArray<const FMountedPackage*> Candidates;
		for (const FMountedPackage& Package : Packages)
		{
			Candidates.Add(&Package);
		}
		for (const FLoadResult& Result : Loader.GetStartupResults())
		{
			if (Result.Error == ELoadError::PlanRejected && Result.Package.ModId.IsEmpty())
			{
				Error = Result.Detail;
				return false;
			}
			if (!Result.IsSuccess() && Result.Error != ELoadError::NotSelected && Result.Package.Extensions.bDeclared &&
			    (!Selection.Enabled || Selection.Enabled->Contains(Result.Package.ModId)) &&
			    !Packages.ContainsByPredicate([&Result](const auto& Package)
			                                  { return Package.ModId == Result.Package.ModId; }))
			{
				Error =
				    TEXT("Could not prepare extension package ") + Result.Package.ModId + TEXT(": ") + Result.Detail;
				return false;
			}
		}
		TArray<int32> Sorted;
		if (!ResolvePackageOrder(Candidates, Selection, Sorted, Error))
		{
			return false;
		}
		auto Session = MakeShared<FModExtensionSession>();
		Session->Admission = Loader.PinPackageSession();
		Session->Grants = Policy.Grants;
		for (int32 Index : Sorted)
		{
			auto Lease = Loader.PinMountedPackage(Packages[Index].ModId);
			if (!Lease)
			{
				Error = TEXT("Package became unavailable: ") + Packages[Index].ModId;
				return false;
			}
			if (!Session->Grants.Contains(Lease->ModId))
			{
				Session->Grants.Add(Lease->ModId, Lease->GrantedCapabilities);
			}
			Session->Packages.Add(MoveTemp(Lease));
		}
		Host.ExtensionSession = MoveTemp(Session);
	}
	else if (Selection.Enabled || !Selection.Required.IsEmpty() || !Selection.Archives.IsEmpty())
	{
		TArray<const FMountedPackage*> Candidates;
		for (const auto& Package : Host.ExtensionSession->Packages)
		{
			Candidates.Add(Package.Get());
		}
		TArray<int32> Sorted;
		if (!ResolvePackageOrder(Candidates, Selection, Sorted, Error))
		{
			return false;
		}
		if (Sorted.Num() != Candidates.Num())
		{
			Error = TEXT("Changing the selected package set requires a new game session.");
			return false;
		}
	}
	Policy.Grants = Host.ExtensionSession->Grants;
	for (const auto& Package : Host.ExtensionSession->Packages)
	{
		for (const FName Grant : Package->Extensions.RequiredGrants)
		{
			if (!HasGrant(Package->ModId, Grant))
			{
				Error = Package->ModId + TEXT(": missing World grant ") + Grant.ToString();
				return false;
			}
		}
		Mods.AddDefaulted_GetRef().Package = Package;
	}
	return true;
}

bool FModExtensionWorld::Prepare(FString& Error)
{
	UWorld* Current = World.Get();
	auto* Game = Current ? Current->GetGameInstance() : nullptr;
	auto* APIHost = Game ? Game->GetSubsystem<UModAPIHostSubsystem>() : nullptr;
	if (!APIHost || !SelectPackages(*APIHost, Error))
	{
		if (Error.IsEmpty())
		{
			Error = TEXT("The World has no game-instance extension host.");
		}
		return false;
	}
	APIRegistry = APIHost->GetRegistry();
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
	Channels.Reset();
	TArray<FDefinition> Definitions;
	FGeneratedRegistration::DescribeAll(Definitions);
	for (FDefinition& Definition : Definitions)
	{
		if (Definition.Id.IsNone() || Channels.Contains(Definition.Id))
		{
			Error = TEXT("Invalid or duplicate extension ID: ") + Definition.Id.ToString();
			return false;
		}
		if (Definition.Scalar == EScalar::Projected &&
		    (!Definition.Payload.Prepare() ||
		     (Definition.Payload.IsObject() &&
		      (!APIRegistry ||
		       !APIRegistry->HasServiceType(Definition.Payload.TypeId, *Definition.Payload.Signature)))))
		{
			Error = TEXT("The reflected extension payload does not match its generated contract: ") +
			        Definition.Id.ToString();
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
	for (const auto& Mod : Mods)
	{
		const auto& Package = Mod.Package;
		for (const FServiceOffer& Offer : Package->Extensions.Provides)
		{
			if (!APIRegistry || !APIRegistry->HasServiceType(Offer.TypeId, *Offer.Signature) ||
			    !HasGrant(Package->ModId, Offer.Permission))
			{
				Error = Package->ModId + TEXT(": unavailable service interface or grant ") + Offer.Id.ToString();
				return false;
			}
		}
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
	auto ValidatePreparation = MoveTemp(Policy.ValidatePreparation);
	if ((ValidatePreparation && !ValidatePreparation(Error)) || Phase != EState::Preparing || !ResolveContent(Error) ||
	    !PreparePatches(Error))
	{
		return false;
	}
	if (Phase != EState::Preparing)
	{
		Error = TEXT("World closed while extension resources were preparing.");
		return false;
	}
	if (auto StageSavedState = MoveTemp(Policy.StageSavedState))
	{
		TGuardValue<bool> StagingSave(bStagingSavedState, true);
		if (!StageSavedState(Error) || Phase != EState::Preparing)
		{
			return false;
		}
	}
	for (int32 Index = 0; Index < Mods.Num(); ++Index)
	{
		FMod& Mod = Mods[Index];
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
			if (!IsValid(Instance) || Phase != EState::Preparing)
			{
				Error = Mod.Package->ModId + TEXT(": lifecycle instance could not be prepared.");
				return false;
			}
			UModInstance::SetExtensionWorld(*Instance, *Current, Mod.Package->ModId);
			Instances.Add(FObjectKey(Instance), Index);
			Mod.Instances.Emplace(Instance);
		}
		if (!Mod.Package->NativeExtensionPath.IsEmpty())
		{
			auto* Instance = NewObject<UModNativeExtensionInstance>(Game);
			UModInstance::SetExtensionWorld(*Instance, *Current, Mod.Package->ModId);
			Instances.Add(FObjectKey(Instance), Index);
			Mod.NativeOwner.Reset(Instance);
			Mod.Native = MakeShared<FNativeExtension>();
			if (!Mod.Native->Open(*Mod.Package, *Instance, AsShared(), Error))
			{
				return false;
			}
		}
	}
	for (FMod& Mod : Mods)
	{
		for (const auto& Instance : Mod.Instances)
		{
			++Mod.RegisteredInstances;
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
	if (!ValidateServices(Error, false))
	{
		return false;
	}
	if (!RefreshSnapshots(Error))
	{
		return false;
	}
	if (!Policy.ExpectedGameplayDigest.IsEmpty() &&
	    Patches.ContainsByPredicate([](const FPatch& Patch)
	                                { return Patch.Definition.Operation != EWorldPatchOperation::RegisterData; }))
	{
		const FString PlannedDigest = CalculateGameplayDigest();
		if (Phase != EState::Preparing || PlannedDigest != Policy.ExpectedGameplayDigest)
		{
			Error = TEXT("The planned World does not match the expected gameplay digest.");
			Fail(EError::SignatureMismatch, Error);
			return false;
		}
	}
	if (!ApplyPatches(true, Error) || !ValidateServices(Error))
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
	if (!ValidateServices(Error) || (bDirty && !RefreshSnapshots(Error)))
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

bool FModExtensionWorld::ValidateServices(FString& Error, bool bAfterPatches) const
{
	for (const FMod& Mod : Mods)
	{
		for (const auto& Need : Mod.Package->Extensions.Services)
		{
			if (Need.bRequired &&
			    ((bAfterPatches && World.IsValid() && World->GetNetMode() != NM_Client) || !IsPatchService(Need)) &&
			    !IsServiceAvailable(Need, Mod.Package->ModId))
			{
				Error =
				    Mod.Package->ModId + TEXT(": incompatible, unauthorized or missing service ") + Need.Id.ToString();
				return false;
			}
		}
	}
	return true;
}

bool FModExtensionWorld::IsServiceAvailable(const FServiceDependency& Need, const FString& Consumer) const
{
	const FServiceEntry* Entry = Services.Find(Need.Id);
	if (!Entry || (Entry->PatchIndex == INDEX_NONE && !Entry->Owner.IsValid()) || !IsValid(Entry->Target.Get()) ||
	    Entry->ModId != Need.Provider || Entry->Definition.TypeId != Need.TypeId ||
	    Entry->Definition.Signature != Need.Signature || !Need.Versions.Contains(Entry->Definition.Version) ||
	    !HasGrant(Consumer, Entry->Definition.Permission))
	{
		return false;
	}
	const auto* Actor = Cast<AActor>(Entry->Target.Get());
	return !Actor || !Actor->IsActorBeingDestroyed();
}

bool FModExtensionWorld::IsPatchService(const FServiceDependency& Need) const
{
	for (const auto& Mod : Mods)
	{
		if (Mod.Package->ModId == Need.Provider)
		{
			return Mod.Package->Extensions.Provides.ContainsByPredicate(
			    [&Need](const auto& Offer) { return Offer.Id == Need.Id && !Offer.Patch.IsNone(); });
		}
	}
	return false;
}

FString FModExtensionWorld::CalculateGameplayDigest() const
{
	FString Canonical;
	const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Canonical);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("policyVersion"), 2);
	if (const auto* Current = World.Get())
	{
		Writer->WriteValue(TEXT("world"), UWorld::RemovePIEPrefix(Current->GetOutermost()->GetName()));
	}
	Writer->WriteArrayStart(TEXT("packages"));
	for (const FMod& Mod : Mods)
	{
		const auto& Package = *Mod.Package;
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("id"), Package.ModId);
		Writer->WriteValue(TEXT("version"), Package.Version);
		const FString ReleaseId =
		    Policy.GameplayReleaseId ? Policy.GameplayReleaseId(Package) : Package.GameplayRelease.ReleaseId;
		if (ReleaseId.IsEmpty())
		{
			return {};
		}
		Writer->WriteValue(TEXT("release"), ReleaseId);
		Writer->WriteValue(TEXT("compatibility"), Package.GameplayRelease.CompatibilityDigest);
		TArray<FName> Grants = Policy.Grants.FindRef(Package.ModId).Array();
		Grants.Sort(FNameLexicalLess());
		Writer->WriteArrayStart(TEXT("grants"));
		for (const FName Grant : Grants)
		{
			Writer->WriteValue(Grant.ToString().ToLower());
		}
		Writer->WriteArrayEnd();
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	TArray<FName> ChannelIds;
	Channels.GetKeys(ChannelIds);
	ChannelIds.Sort(FNameLexicalLess());
	Writer->WriteArrayStart(TEXT("channels"));
	for (const FName Id : ChannelIds)
	{
		const auto& Channel = Channels.FindChecked(Id);
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("id"), Id.ToString().ToLower());
		Writer->WriteValue(TEXT("signature"), Channel->GetDefinition().Signature);
		TArray<FString> Order;
		Channel->DescribeOrder(Order);
		Writer->WriteValue(TEXT("order"), Order);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	TArray<FContentKey> Keys;
	Contents.GetKeys(Keys);
	Keys.Sort(
	    [](const FContentKey& Left, const FContentKey& Right)
	    { return Left.Key == Right.Key ? Left.Value.LexicalLess(Right.Value) : Left.Key.LexicalLess(Right.Key); });
	Writer->WriteArrayStart(TEXT("content"));
	for (const auto& Key : Keys)
	{
		const auto& Choice = Contents.FindChecked(Key);
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("slot"), Key.Key.ToString().ToLower());
		Writer->WriteValue(TEXT("id"), Key.Value.ToString().ToLower());
		Writer->WriteValue(TEXT("status"), static_cast<int32>(Choice.Status));
		Writer->WriteValue(TEXT("mod"), Choice.ModId);
		Writer->WriteValue(TEXT("asset"), Choice.Path.ToString());
		Writer->WriteValue(TEXT("fallback"), Choice.bUsedFallback);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	TMap<FName, TPair<FString, FService>> ChosenServices;
	for (const FMod& Mod : Mods)
	{
		for (const FServiceOffer& Offer : Mod.Package->Extensions.Provides)
		{
			ChosenServices.Emplace(
			    Offer.Id, TPair<FString, FService>(Mod.Package->ModId, FService{Offer.Id, Offer.TypeId, Offer.Version,
			                                                                    Offer.Signature, Offer.Permission}));
		}
	}
	for (const auto& [Id, Entry] : Services)
	{
		ChosenServices.FindOrAdd(Id, TPair<FString, FService>(Entry.ModId, Entry.Definition));
	}
	TArray<FName> ServiceIds;
	ChosenServices.GetKeys(ServiceIds);
	ServiceIds.Sort(FNameLexicalLess());
	Writer->WriteArrayStart(TEXT("services"));
	for (const FName Id : ServiceIds)
	{
		const auto& Entry = ChosenServices.FindChecked(Id);
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("id"), Id.ToString().ToLower());
		Writer->WriteValue(TEXT("mod"), Entry.Key);
		Writer->WriteValue(TEXT("version"), Entry.Value.Version);
		Writer->WriteValue(TEXT("signature"), Entry.Value.Signature);
		Writer->WriteValue(TEXT("permission"),
		                   Entry.Value.Permission.IsNone() ? FString() : Entry.Value.Permission.ToString().ToLower());
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	Writer->WriteArrayStart(TEXT("patches"));
	for (const FPatch& Patch : Patches)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("id"), Patch.Definition.Id.ToString().ToLower());
		Writer->WriteValue(TEXT("mod"), Patch.ModId);
		Writer->WriteValue(TEXT("anchor"), Patch.Definition.Anchor.ToString().ToLower());
		Writer->WriteValue(TEXT("operation"), static_cast<int32>(Patch.Definition.Operation));
		Writer->WriteValue(TEXT("actorClass"), Patch.Definition.ActorClass.ToString());
		Writer->WriteValue(TEXT("componentClass"), Patch.Definition.ComponentClass.ToString());
		Writer->WriteValue(TEXT("dataId"), Patch.Definition.DataId.ToString().ToLower());
		Writer->WriteValue(TEXT("dataAsset"), Patch.Definition.DataAsset.ToString());
		Writer->WriteValue(TEXT("dependencies"), Patch.RequiredPatches);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	return Writer->Close() ? HashPackagePlan(Canonical) : FString();
}

void FModExtensionWorld::DispatchQueuedEvents()
{
	constexpr int32 PerTickBudget = 64;
	const int32 Count = FMath::Min(PerTickBudget, QueuedEvents.Num());
	if (Count == 0 || Phase != EState::Ready || Depth != 0)
	{
		return;
	}
	TArray<TUniqueFunction<void()>, TInlineAllocator<PerTickBudget>> Batch;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Batch.Add(MoveTemp(QueuedEvents[Index]));
	}
	QueuedEvents.RemoveAt(0, Count, EAllowShrinking::No);
	for (auto& Dispatch : Batch)
	{
		if (Phase != EState::Ready)
		{
			break;
		}
		++Depth;
		Dispatch();
		FinishDispatch();
	}
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
	else if (Phase == EState::Ready)
	{
		FString Error;
		if (bDirty && !RefreshSnapshots(Error))
		{
			UE_LOG(LogModExtensionWorld, Error, TEXT("%s"), *Error);
		}
	}
}

void FModExtensionWorld::Fail(EError Error, FString Detail, bool bRestartRequired)
{
	if (Failure.Error == EError::None || (bRestartRequired && !Failure.bRestartRequired))
	{
		Failure = {Error, MoveTemp(Detail), bRestartRequired};
	}
	Phase = EState::Draining;
	if (bRestartRequired)
	{
		IModLoaderModule::Get().RequireRestart(Failure.Detail);
	}
	Drain();
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
	QueuedEvents.Reset();
	Channels.Reset();
	BlueprintCalls.Reset();
	for (int32 Index = Mods.Num() - 1; Index >= 0; --Index)
	{
		const FMod& Mod = Mods[Index];
		if (Mod.Native)
		{
			Mod.Native->Close();
			if (Mod.NativeOwner)
			{
				Mod.NativeOwner->ReleaseExtensions(false);
				Instances.Remove(FObjectKey(Mod.NativeOwner.Get()));
			}
		}
		for (int32 Entry = Mod.Instances.Num() - 1; Entry >= 0; --Entry)
		{
			const auto& Instance = Mod.Instances[Entry];
			if (Instance)
			{
				TGuardValue<TOptional<FObjectKey>> Owner(CleanupOwner,
				                                         TOptional<FObjectKey>(FObjectKey(Instance.Get())));
				Instance->ReleaseExtensions(Entry < Mod.RegisteredInstances);
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
			if (Entry.Value().PatchIndex == INDEX_NONE && Entry.Value().ModId == Mod.Package->ModId)
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
	RevertPatches();
	PatchData.Reset();
	Patches.Reset();
	Contents.Reset();
	PendingContent.Reset();
	LoadingContent.Reset();
	ContentLeases.Reset();
	Callers.Reset();
	Services.Reset();
	Mods.Reset();
	Anchors.Reset();
	RemovedAnchors.Reset();
	Phase = EState::Closed;
	if (Failure.Error != EError::None)
	{
		UE_LOG(LogModExtensionWorld, Error, TEXT("%s"), *Failure.Detail);
		auto Handler = MoveTemp(FailureHandler);
		Handler.ExecuteIfBound(Failure);
	}
}
