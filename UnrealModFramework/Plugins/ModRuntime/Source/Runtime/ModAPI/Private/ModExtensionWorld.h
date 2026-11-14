// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "ModAPINative.h"
#include "ModExtensionSubsystem.h"
#include "ModLoader.h"
#include "ModInstance.h"

class AActor;
class UModAPIHostSubsystem;

struct FModExtensionWorld
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
	};

	struct FAnchor
	{
		TWeakObjectPtr<ULevel> Level;
		FTransform Transform;
		FName Permission;
		uint64 Generation = 0;
	};

	struct FPatch
	{
		FString ModId;
		Mod::Loader::FWorldPatch Definition;
		TStrongObjectPtr<UClass> Class;
	};

	struct FAppliedPatch
	{
		int32 PatchIndex = INDEX_NONE;
		uint64 AnchorGeneration = 0;
		TWeakObjectPtr<AActor> Actor;
	};

	struct FServiceEntry
	{
		TWeakObjectPtr<UModInstance> Owner;
		FString ModId;
		Mod::Extension::FService Definition;
		Mod::API::FRegistration Registration;
		TStrongObjectPtr<UObject> Target;
	};

	explicit FModExtensionWorld(UWorld& InWorld, uint64 InGeneration);
	bool Prepare(FString& Error);
	void FinishDispatch();
	void Drain();
	bool RefreshSnapshots(FString& Error);
	bool ValidateServices(FString& Error) const;
	bool ResolveContent(FString& Error);
	bool PreparePatches(FString& Error);
	bool ApplyPatches(bool bInitial, FString& Error);
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
	bool bBindingServices = false;
	bool bStopping = false;
	uint64 LastAnchor = 0;
	FWorldPolicy Policy;
	TMap<FName, TSharedPtr<FChannel>> Channels;
	TMap<TPair<FName, TWeakObjectPtr<UClass>>, TSharedPtr<const Mod::API::FBlueprintCall>> BlueprintCalls;
	TMap<FName, Mod::Extension::FContentSlot> Slots;
	TMap<FContentKey, Mod::Extension::FResolvedContent> Contents;
	TArray<TStrongObjectPtr<UObject>> ContentLeases;
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
	TArray<FString> FailedCompensations;
	TArray<FPatch> Patches;
	TArray<FAppliedPatch> Applied;
};
