// Copyright (c) 2026 Nelaric Contributors
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, SharedRuntimeChecks);

#if WITH_DEV_AUTOMATION_TESTS && !WITH_EDITOR
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameCharacter.h"
#include "GameFramework/PlayerController.h"
#include "GameLocalPlayerAPI.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "ModAPIHostSubsystem.h"
#include "ModExtensionSubsystem.h"
#include "ModInstance.h"
#include "ModLoader.h"
#include "UMFDevSDKAPI.h"
#include "UnrealModFrameworkDev.modabi.h"
#include "UnrealModFrameworkDev.modcalls.h"
#include "UnrealModFrameworkDev.moderrors.h"

namespace
{
using FCharacter = FModObject_T_f2e0514f60055d77;
using ESdkOutcome = EUMFDevSDKCallOutcome;
const TCHAR* const HookId = TEXT("hook/game.character/change-health");
const TCHAR* const SlotId = TEXT("character.mesh");
const TCHAR* const ContentId = TEXT("character.mannequin");

class FSharedCalls : public IAutomationLatentCommand
{
public:
	explicit FSharedCalls(FAutomationTestBase& InTest) : Test(InTest)
	{
		FParse::Value(FCommandLine::Get(), TEXT("SharedRemovePath="), RemovePath);
	}

	~FSharedCalls()
	{
		if (BeforeUnload.IsValid())
		{
			Mod::Loader::IModLoaderModule::Get().OnBeforeUnload().Remove(BeforeUnload);
		}
	}

	bool Update() override
	{
		if (RemovePath.IsEmpty() || FPlatformTime::Seconds() > Deadline)
		{
			Test.AddError(FString::Printf(TEXT("Shared Runtime acceptance did not finish in phase %d."),
			                              static_cast<int32>(Phase)));
			return true;
		}
		for (const FWorldContext& Entry : GEngine->GetWorldContexts())
		{
			UWorld* World = Entry.World();
			UGameInstance* Game = World ? World->GetGameInstance() : nullptr;
			ULocalPlayer* Player = Game ? Game->GetFirstGamePlayer() : nullptr;
			APlayerController* Controller = Player ? Player->GetPlayerController(World) : nullptr;
			auto* Pawn = Controller ? Cast<AGameCharacter>(Controller->GetPawn()) : nullptr;
			auto* Host = Game ? Game->GetSubsystem<UModAPIHostSubsystem>() : nullptr;
			auto* Extensions = World ? World->GetSubsystem<UModExtensionSubsystem>() : nullptr;
			if (!Host || !Pawn || !Extensions)
			{
				continue;
			}
			switch (Phase)
			{
			case EPhase::BothMods:
				if (Mod::Loader::IModLoaderModule::Get().GetMountedPackages().Num() == 2)
				{
					return !CheckBoth(*World, *Pawn, *Host, *Player, *Extensions);
				}
				break;
			case EPhase::WaitForRemoval:
				if (!FPaths::FileExists(RemovePath))
				{
					Test.TestTrue(TEXT("The active World pins the removed archive"),
					              FPackageName::MountPointExists(TEXT("/TestMod/")));
					Pawn->RestoreMesh();
					Extensions->Stop();
					Phase = EPhase::WaitForUnmount;
				}
				break;
			case EPhase::WaitForUnmount:
				if (!FPackageName::MountPointExists(TEXT("/TestMod/")))
				{
					Test.TestTrue(TEXT("Unloading TestMod revoked the writer in the live World"), bRevoked);
					Test.TestEqual(TEXT("The unloaded caller loses read access before travel"),
					               ReadHealth(FirstContext), -1.0f);
					Phase = EPhase::SurvivingMod;
					UGameplayStatics::OpenLevel(World, TEXT("/Game/Levels/Basic/L_Basic"), true,
					                            TEXT("game=/Script/SharedRuntimeChecks.SharedRuntimeTestMode"));
				}
				break;
			case EPhase::SurvivingMod:
				if (World != FirstWorld.Get())
				{
					CheckSurvivor(*Pawn, *Host, *Player, *Extensions);
					return true;
				}
				break;
			}
		}
		return false;
	}

private:
	enum class EPhase
	{
		BothMods,
		WaitForRemoval,
		WaitForUnmount,
		SurvivingMod,
	};

	static float ReadHealth(const FUMFDevSDKPlayerContext& Context)
	{
		FCharacter Character;
		ESdkOutcome Outcome{};
		int64 Error = 0;
		UUMFDevSDKLibrary::GetLocalCharacter(Context, Character, Outcome, Error);
		if (Error)
		{
			return -1;
		}
		float Health = -1;
		UUMFDevSDKLibrary::GetCurrentHealth(Character, Health, Outcome, Error);
		return Error ? -1 : Health;
	}

	bool Prepare(UModExtensionSubsystem& Extensions, bool bIncludeFirst)
	{
		Mod::Extension::FWorldPolicy Policy;
		Policy.EnabledMods.Add(TEXT("sharedcaller"));
		Policy.Grants.Add(TEXT("sharedcaller"), {FName(HookId), TEXT("WorldRead")});
		Mod::Extension::FContentSlot Meshes;
		Meshes.AssetType = USkeletalMesh::StaticClass();
		Meshes.bAllowReplace = true;
		Meshes.Defaults.Add(
		    ContentId, FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
		if (!Test.TestTrue(TEXT("Install the game-owned character mesh slot"),
		                   Extensions.DefineContentSlot(SlotId, MoveTemp(Meshes))))
		{
			return false;
		}
		if (bIncludeFirst)
		{
			Policy.EnabledMods.Add(TEXT("testmod"));
			Policy.Grants.Add(TEXT("testmod"), {FName(HookId), TEXT("WorldRead"), TEXT("Content.character.mesh")});
		}
		FString Error;
		const bool bReady = Extensions.Prepare(MoveTemp(Policy), Error);
		return Test.TestTrue(FString::Printf(TEXT("Prepare the World extension plan: %s"), *Error), bReady);
	}

	bool CheckBoth(UWorld& World, AGameCharacter& Pawn, UModAPIHostSubsystem& Host, ULocalPlayer& Player,
	               UModExtensionSubsystem& Extensions)
	{
		auto Query = reinterpret_cast<decltype(&UMF_UnrealModFrameworkDev_ExportTest_GetHostAPIV1)>(
		    UModInstance::ResolveHostAPI("ExportTest", "UMF_UnrealModFrameworkDev_ExportTest_GetHostAPIV1"));
		UMF_UnrealModFrameworkDev_ExportTest_FHostAPIV1 Table{};
		if (!Test.TestTrue(TEXT("Host rejects a different ABI hash"),
		                   Query && Query(1, sizeof(Table), "", &Table) != 0) ||
		    !Test.TestEqual(TEXT("Bind the installed SDK Runtime to the game"),
		                    UUMFDevSDKLibrary::ConnectHost(&UModInstance::ResolveHostAPI), int64(0)) ||
		    !Prepare(Extensions, true))
		{
			return false;
		}
		QueryAddress = reinterpret_cast<void*>(Query);
		const auto A = Host.OpenLocalPlayer<UGameLocalPlayerAPI>(&Player, {TEXT("WorldRead"), TEXT("WorldWrite")});
		const auto B = Host.OpenLocalPlayer<UGameLocalPlayerAPI>(&Player, {TEXT("WorldRead")});
		if (!Test.TestTrue(TEXT("Independent SDK callers"),
		                   A.IsSuccess() && B.IsSuccess() &&
		                       A.GetValue().Native.Context != B.GetValue().Native.Context))
		{
			return false;
		}
		const auto& AToken = A.GetValue().Native;
		const auto& BToken = B.GetValue().Native;
		FirstContext = FUMFDevSDKPlayerContext::FromNative(AToken.Context, AToken.Scope);
		const auto SecondContext = FUMFDevSDKPlayerContext::FromNative(BToken.Context, BToken.Scope);
		const auto FirstCaller = A.GetValue().Caller;
		BeforeUnload = Mod::Loader::IModLoaderModule::Get().OnBeforeUnload().AddLambda(
		    [this, Host = TWeakObjectPtr<UModAPIHostSubsystem>(&Host),
		     FirstCaller](const Mod::Loader::FMountedPackage& Package)
		    {
			    if (Package.ModId == TEXT("testmod") && Host.IsValid())
			    {
				    bRevoked = Host->RevokeCaller(FirstCaller).IsSuccess();
			    }
		    });

		const auto Choice = Extensions.ResolveContent(SlotId, ContentId);
		auto* Replacement = Cast<USkeletalMesh>(Choice.Path.ResolveObject());
		Test.TestTrue(TEXT("The extension selected TestMod's cooked Manny"),
		              Choice.ModId == TEXT("testmod") && Replacement &&
		                  Choice.Path.ToString().StartsWith(TEXT("/TestMod/")));

		FCharacter Character;
		ESdkOutcome Outcome{};
		int64 Error = 0;
		bool bAccepted = false;
		UUMFDevSDKLibrary::GetLocalCharacter(SecondContext, Character, Outcome, Error);
		Test.TestEqual(TEXT("Reader can resolve the character"), Error, int64(0));
		UUMFDevSDKLibrary::SetMesh(Character, Replacement, bAccepted, Outcome, Error);
		Test.TestEqual(TEXT("Reader cannot replace the mesh"), Error, int64(MOD_ERROR_PERMISSIONDENIED));
		UUMFDevSDKLibrary::GetLocalCharacter(FirstContext, Character, Outcome, Error);
		Test.TestEqual(TEXT("Writer can resolve the character"), Error, int64(0));
		UUMFDevSDKLibrary::SetMesh(Character, Replacement, bAccepted, Outcome, Error);
		Test.TestTrue(TEXT("Writer applies the selected mesh through the installed SDK"),
		              !Error && bAccepted && Replacement && Pawn.GetMesh()->GetSkeletalMeshAsset() == Replacement);

		Pawn.RestoreSaveHealth(63);
		Test.TestEqual(TEXT("Distinct native callbacks add 4 and 6"), Pawn.ChangeHealth(0), 73.0f);
		const float Health = ReadHealth(SecondContext);
		Test.TestEqual(TEXT("Reader sees the result of both native callbacks"), Health, 73.0f);
		Host.RevokeCaller(B.GetValue().Caller);
		FirstWorld = &World;
		Phase = EPhase::WaitForRemoval;
		UE_LOG(LogTemp, Display, TEXT("UMF_SHARED_BOTH_CALLED mesh=%s health=%.0f ABI=%hs"),
		       *GetPathNameSafe(Replacement), Health, UE_UMF_UNREALMODFRAMEWORKDEV_ABI_HASH);
		return true;
	}

	void CheckSurvivor(AGameCharacter& Pawn, UModAPIHostSubsystem& Host, ULocalPlayer& Player,
	                   UModExtensionSubsystem& Extensions)
	{
		const auto Mounted = Mod::Loader::IModLoaderModule::Get().GetMountedPackages();
		Test.TestTrue(TEXT("Only the second Mod remains mounted"),
		              Mounted.Num() == 1 && Mounted[0].ModId == TEXT("sharedcaller"));
		Test.TestTrue(TEXT("Both World plans use the same live host ABI"),
		              QueryAddress &&
		                  QueryAddress == UModInstance::ResolveHostAPI(
		                                      "ExportTest", "UMF_UnrealModFrameworkDev_ExportTest_GetHostAPIV1"));
		if (!Prepare(Extensions, false))
		{
			return;
		}
		const auto Choice = Extensions.ResolveContent(SlotId, ContentId);
		Test.TestTrue(TEXT("The next World resolves the host mesh after removing the replacement"),
		              Choice.ModId.IsEmpty() && Choice.Path.ToString().EndsWith(TEXT("SKM_Quinn_Simple")) &&
		                  Choice.Path.ResolveObject());
		const auto B = Host.OpenLocalPlayer<UGameLocalPlayerAPI>(&Player, {TEXT("WorldRead")});
		if (!Test.TestTrue(TEXT("Reader can open a scope after travel"), B.IsSuccess()))
		{
			return;
		}
		const auto& Token = B.GetValue().Native;
		Pawn.RestoreSaveHealth(35);
		Test.TestEqual(TEXT("Only the surviving callback adds 6"), Pawn.ChangeHealth(0), 41.0f);
		const float Health = ReadHealth(FUMFDevSDKPlayerContext::FromNative(Token.Context, Token.Scope));
		const float Expired = ReadHealth(FirstContext);
		Test.TestEqual(TEXT("The surviving reader uses the connected SDK Runtime"), Health, 41.0f);
		Test.TestEqual(TEXT("The revoked writer loses read access too"), Expired, -1.0f);
		Host.RevokeCaller(B.GetValue().Caller);
		UE_LOG(LogTemp, Display, TEXT("UMF_SHARED_AFTER_UNLOAD health=%.0f revoked=%.0f"), Health, Expired);
	}

	FAutomationTestBase& Test;
	const double Deadline = FPlatformTime::Seconds() + 120;
	FString RemovePath;
	TWeakObjectPtr<UWorld> FirstWorld;
	void* QueryAddress = nullptr;
	FUMFDevSDKPlayerContext FirstContext;
	FDelegateHandle BeforeUnload;
	EPhase Phase = EPhase::BothMods;
	bool bRevoked = false;
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedRuntimeTest, "Nelaric.UnrealModFramework.SharedRuntime.TwoMods",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSharedRuntimeTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSharedCalls(*this));
	return true;
}
#endif
