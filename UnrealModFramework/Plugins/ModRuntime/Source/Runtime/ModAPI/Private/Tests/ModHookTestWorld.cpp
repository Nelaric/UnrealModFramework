// Copyright (c) 2026 Nelaric Contributors
#include "ModHookTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "ModExtensionWorld.h"
#include "ModNativeExtension.h"

namespace Mod::Tests
{
bool FHookTestWorld::Run(UWorld& World, TArray<Extension::FDefinition> Definitions, TSet<FName> Grants,
                         TFunctionRef<bool(UModExtensionSubsystem&, UModInstance&)> Test)
{
	auto* Subsystem = World.GetSubsystem<UModExtensionSubsystem>();
	if (!GIsAutomationTesting || !Subsystem || !Subsystem->State ||
	    Subsystem->State->Phase != FModExtensionWorld::EState::New)
	{
		return false;
	}
	auto& State = *Subsystem->State;
	auto* Owner = NewObject<UModNativeExtensionInstance>(&World);
	const FString ModId = TEXT("hook-automation");
	UModInstance::SetExtensionWorld(*Owner, World, ModId);
	auto Package = MakeShared<Loader::FMountedPackage>();
	Package->ModId = ModId;
	Package->ArchiveSha256 = FString::ChrN(64, TEXT('a'));
	Package->GameplayRelease.ReleaseId = TEXT("automation-release");
	Package->GameplayRelease.CompatibilityDigest = Package->ArchiveSha256;
	for (const auto& Definition : Definitions)
	{
		Package->Extensions.Contracts.Add({Definition.Id, Definition.Signature});
	}
	FString Error;
	if (!State.InstallDefinitions(MoveTemp(Definitions), Error))
	{
		UE_LOG(LogTemp, Error, TEXT("%s"), *Error);
		return false;
	}
	auto& Mod = State.Mods.AddDefaulted_GetRef();
	Mod.Package = Package;
	Mod.Instances.Emplace(Owner);
	State.Instances.Add(FObjectKey(Owner), 0);

	State.Policy.Grants.Add(ModId, MoveTemp(Grants));
	State.Phase = FModExtensionWorld::EState::Ready;
	ON_SCOPE_EXIT
	{
		Subsystem->Stop();
	};
	return Test(*Subsystem, *Owner);
}
} // namespace Mod::Tests

#endif
