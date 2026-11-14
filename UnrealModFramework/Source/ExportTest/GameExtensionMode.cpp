// Copyright (c) 2026 Nelaric Contributors
#include "GameExtensionMode.h"

#include "Engine/SkeletalMesh.h"
#include "GameCharacter.h"
#include "Engine/World.h"
#include "ModLoader.h"
#include "ModExtensionSubsystem.h"

AGameExtensionMode::AGameExtensionMode()
{
	DefaultPawnClass = AGameCharacter::StaticClass();
}

void AGameExtensionMode::StartPlay()
{
	auto* World = GetWorld();
	auto* Extensions = World->GetSubsystem<UModExtensionSubsystem>();
	if (!Extensions)
	{
		Super::StartPlay();
		return;
	}
	Mod::Extension::FContentSlot Meshes;
	Meshes.AssetType = USkeletalMesh::StaticClass();
	Meshes.bAllowReplace = true;
	Meshes.bAllowDisable = true;
	Meshes.Defaults.Add(TEXT("character.mannequin"),
	                    FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
	Extensions->DefineContentSlot(TEXT("character.mesh"), MoveTemp(Meshes));
	Extensions->RegisterAnchor(TEXT("training.target"), *World->PersistentLevel, FTransform(FVector(0, 0, 100)),
	                           TEXT("WorldPatch.training.target"));
	Mod::Extension::FWorldPolicy Policy;
	for (const auto& Package : Mod::Loader::IModLoaderModule::Get().GetMountedPackages())
	{
		// Sample host policy for the loader-approved local mod set.
		Policy.Grants.Add(Package.ModId,
		                  {TEXT("Health.Observe"), TEXT("Health.Change"), TEXT("Content.character.mesh"),
		                   TEXT("WorldPatch.training.target"), TEXT("hook/game.character/change-health.ModifyInputs"),
		                   TEXT("hook/game.character/change-health.Cancel"),
		                   TEXT("hook/game.character/change-health.ModifyResult")});
	}
	FString Error;
	if (!Extensions->Prepare(MoveTemp(Policy), Error))
	{
		UE_LOG(LogTemp, Error, TEXT("Could not start extension gameplay: %s"), *Error);
		return;
	}
	Super::StartPlay();
}
