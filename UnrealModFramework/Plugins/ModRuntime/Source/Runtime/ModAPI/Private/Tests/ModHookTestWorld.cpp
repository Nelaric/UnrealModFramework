// Copyright (c) 2026 Nelaric Contributors
#include "ModHookTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "ModExtensionWorld.h"
#include "ModNativeExtension.h"

namespace Mod::Tests
{
bool FHookTestWorld::Run(UWorld& World, Extension::FDefinition Definition,
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
	Package->Extensions.Contracts.Add({Definition.Id, Definition.Signature});
	auto& Mod = State.Mods.AddDefaulted_GetRef();
	Mod.Package = Package;
	Mod.Instances.Emplace(Owner);
	State.Instances.Add(FObjectKey(Owner), 0);

	const auto Id = Definition.Id;
	const FString Prefix = Id.ToString() + TEXT(".");
	Definition.ModifyInputsPermission = FName(Prefix + TEXT("ModifyInputs"));
	Definition.CancelPermission = FName(Prefix + TEXT("Cancel"));
	State.Policy.Grants.Add(ModId, {Definition.Permission, Definition.ModifyInputsPermission,
	                                Definition.CancelPermission, FName(Prefix + TEXT("Replace"))});
	State.Channels.Add(Id, MakeShared<Extension::FChannel>(MoveTemp(Definition)));
	State.Phase = FModExtensionWorld::EState::Ready;
	ON_SCOPE_EXIT
	{
		Subsystem->Stop();
	};
	return Test(*Subsystem, *Owner);
}
} // namespace Mod::Tests

#endif
