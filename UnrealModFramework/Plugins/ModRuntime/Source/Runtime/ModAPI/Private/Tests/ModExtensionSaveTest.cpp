// Copyright (c) 2026 Nelaric Contributors
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "ModExtension.h"
#include "ModExtensionSave.h"
#include "ModHookTestWorld.h"
#include "ModInstance.h"

namespace Mod::Tests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModExtensionSaveTest, "Nelaric.UnrealModFramework.Save.PersistentReferences",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FModExtensionSaveTest::RunTest(const FString&)
{
	using namespace Extension;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
	};
	return FHookTestWorld::Run(
	    *World, {}, {},
	    [this](UModExtensionSubsystem& Extensions, UModInstance& Owner)
	    {
		    FModSavePolicy Policy;
		    Policy.BaseRelease = TEXT("test-release");
		    Policy.ExportContract = TEXT("test-contract");
		    FString Error;
		    FModSaveSnapshot Snapshot;
		    if (!TestTrue(TEXT("Capture an optional Mod"),
		                  FModExtensionSave::Capture(Extensions, Policy, Snapshot, Error)))
		    {
			    AddError(Error);
			    return false;
		    }
		    TestFalse(TEXT("The Mod is optional before adding a reference"), Snapshot.Mods[0].bRequired);
		    Snapshot.References.Add({Owner.GetModId(), {}, TEXT("persistent-object")});
		    if (!TestTrue(TEXT("Recapture the referenced Mod"),
		                  FModExtensionSave::Capture(Extensions, Policy, Snapshot, Error)))
		    {
			    AddError(Error);
			    return false;
		    }
		    TestTrue(TEXT("A persistent reference makes an existing Mod record required"), Snapshot.Mods[0].bRequired);

		    // A save written before the fix can still carry the old optional flag.
		    const FString MissingModId = TEXT("missing-test-mod");
		    Snapshot.Mods[0].ModId = MissingModId;
		    Snapshot.Mods[0].bRequired = false;
		    Snapshot.References[0].ModId = MissingModId;
		    TestFalse(TEXT("Reject a missing referenced Mod in an older save"),
		              FModExtensionSave::Admit(Extensions, Policy, Snapshot, Error));
		    TestTrue(TEXT("The rejection identifies the missing Mod"), Error.Contains(MissingModId));
		    Snapshot.References.Reset();
		    TestTrue(TEXT("An unreferenced optional Mod can still be missing"),
		             FModExtensionSave::Admit(Extensions, Policy, Snapshot, Error));
		    return true;
	    });
}
} // namespace Mod::Tests

#endif
