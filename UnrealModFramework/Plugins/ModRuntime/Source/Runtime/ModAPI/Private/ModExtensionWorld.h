// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "ModAPINative.h"
#include "ModExtensionSubsystem.h"
#include "ModLoader.h"
#include "ModInstance.h"

class AActor;
class UModAPIHostSubsystem;
namespace Mod::Extension
{
class FNativeExtension;
}

struct FModExtensionSession
{
	TSharedPtr<const Mod::Loader::FPackageSessionLease> Admission;
	TMap<FString, TSet<FName>> Grants;
	TArray<TSharedPtr<const Mod::Loader::FMountedPackage>> Packages;
};

struct FModExtensionWorld : TSharedFromThis<FModExtensionWorld>
{
	using FContentKey = Mod::Extension::FContentKey;
	using FChannel = Mod::Extension::FChannel;
	using FWorldPolicy = Mod::Extension::FWorldPolicy;

	enum class EState : uint8
	{
		New,
		Preparing,
		Ready,
		Draining,
		Closed,
	};

	struct FMod
	{
		TSharedPtr<const Mod::Loader::FMountedPackage> Package;
		TArray<TStrongObjectPtr<UModInstance>> Instances;
		int32 RegisteredInstances = 0;
		TSharedPtr<Mod::Extension::FNativeExtension> Native;
		TStrongObjectPtr<UModInstance> NativeOwner;
	};

	struct FAnchor
	{
		TWeakObjectPtr<ULevel> Level;
		FTransform Transform;
		FName Permission;
		uint64 Generation = 0;
		Mod::Extension::FPatchAnchorRules Rules;
		TStrongObjectPtr<UClass> DataAssetType;
	};

	struct FPatch
	{
		FString ModId;
		Mod::Loader::FWorldPatch Definition;
		FString Revision;
		TStrongObjectPtr<UObject> Asset;
		TArray<FString> RequiredPatches;
		double AnchorDeadline = 0;
		bool bFoundAnchor = false;
	};

	struct FAppliedPatch
	{
		int32 PatchIndex = INDEX_NONE;
		uint64 AnchorGeneration = 0;
		TWeakObjectPtr<UObject> Effect;
		TArray<FString> DependsOn;
	};

	struct FServiceEntry
	{
		TWeakObjectPtr<UModInstance> Owner;
		FString ModId;
		Mod::Extension::FService Definition;
		Mod::API::FRegistration Registration;
		TStrongObjectPtr<UObject> Target;
		int32 PatchIndex = INDEX_NONE;
		uint64 AnchorGeneration = 0;
	};

	explicit FModExtensionWorld(UWorld& InWorld, uint64 InGeneration);
	bool Prepare(FString& Error);
	bool SelectPackages(UModAPIHostSubsystem& Host, FString& Error);
	void Fail(Mod::API::EError Error, FString Detail, bool bRestartRequired = false);
	FString CalculateGameplayDigest() const;
	void FinishDispatch();
	void DispatchQueuedEvents();
	void Drain();
	bool RefreshSnapshots(FString& Error);
	bool ValidateServices(FString& Error, bool bAfterPatches = true) const;
	bool IsPatchService(const Mod::Loader::FServiceDependency& Need) const;
	bool ResolveContent(FString& Error);
	bool LoadContent(const FContentKey& Key, Mod::Extension::FResolvedContent& Content, FString& Error);
	bool PreparePatches(FString& Error);
	bool ApplyPatches(bool bInitial, FString& Error);
	bool SupportsPatch(const FPatch& Patch, const FAnchor& Anchor) const;
	bool CommitPatch(int32 Index, const FAnchor& Anchor, FString& Error);
	Mod::Extension::FPatchOrigin DescribePatch(int32 Index, uint64 AnchorGeneration) const;
	bool RegisterPatchServices(int32 PatchIndex, uint64 AnchorGeneration, AActor& Actor, FString& Error);
	bool IsServiceAvailable(const Mod::Loader::FServiceDependency& Need, const FString& Consumer) const;
	void RevertPatches(uint64 AnchorGeneration = 0);
	bool IsPatchApplied(int32 Index, uint64 AnchorGeneration) const;
	void FlushRemovedAnchors();
	bool HasGrant(const FString& ModId, FName Permission) const;
	bool Owns(const UModInstance& Instance) const;
	bool Admit(const Mod::Extension::FDefinition& Definition) const;

	TWeakObjectPtr<UWorld> World;
	const uint64 Generation;
	EState Phase = EState::New;
	int32 Depth = 0;
	bool bDirty = false;
	FString RegistrationError;
	FString GameplayDigest;
	bool bPatchCommitted = false;
	Mod::Extension::FWorldFailure Failure;
	Mod::Extension::FOnWorldFailure FailureHandler;
	TOptional<FObjectKey> CleanupOwner;
	bool bBindingServices = false;
	bool bStagingSavedState = false;
	bool bStopping = false;
	uint64 LastAnchor = 0;
	FWorldPolicy Policy;
	TMap<FName, TSharedPtr<FChannel>> Channels;
	TArray<TUniqueFunction<void()>> QueuedEvents;
	TMap<TPair<FName, TWeakObjectPtr<UClass>>, TSharedPtr<const Mod::API::FBlueprintCall>> BlueprintCalls;
	TMap<FName, Mod::Extension::FContentSlot> Slots;
	TMap<FContentKey, Mod::Extension::FResolvedContent> Contents;
	TArray<TStrongObjectPtr<UObject>> ContentLeases;
	TSet<FContentKey> PendingContent;
	TSet<FContentKey> LoadingContent;
	TArray<FMod> Mods;
	TMap<FObjectKey, int32> Instances;
	TMap<FName, FServiceEntry> Services;
	TMap<FObjectKey, Mod::API::FCaller> Callers;
	TMap<FObjectKey, Mod::API::FNativeContext> NativeCallers;
	Mod::API::FScope APIScope;
	Mod::API::FRegistry* APIRegistry = nullptr;
	TWeakObjectPtr<UModAPIHostSubsystem> APIHostOwner;
	TMap<FName, FAnchor> Anchors;
	TArray<TPair<FName, TWeakObjectPtr<ULevel>>> RemovedAnchors;
	TArray<FPatch> Patches;
	TArray<FAppliedPatch> Applied;
	TMap<FContentKey, TPair<int32, uint64>> PatchData;
};
