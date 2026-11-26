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
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "ModAPIHostSubsystem.h"
#include "ModInstance.h"
#include "ModLoader.h"
#include "UMFDevSDKAPI.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectIterator.h"
#include "UnrealModFrameworkDev.modabi.h"
#include "UnrealModFrameworkDev.modcalls.h"

namespace
{
UModInstance* FindInstance(UGameInstance* Game, const TCHAR* Id)
{
	for (TObjectIterator<UModInstance> It; It; ++It)
	{
		if (It->GetTypedOuter<UGameInstance>() == Game && It->GetModId() == Id)
		{
			return *It;
		}
	}
	return nullptr;
}

template <class TResult, class TProperty>
bool CallMod(UModInstance& Mod, FName Name, const FUMFDevSDKPlayerContext& Context, TResult& Value)
{
	UFunction* Function = Mod.FindFunction(Name);
	if (!Function)
	{
		return false;
	}
	const FStructProperty* Player = FindFProperty<FStructProperty>(Function, TEXT("PlayerContext"));
	const TProperty* Result = CastField<TProperty>(Function->GetReturnProperty());
	if (!Result || !Player || Player->Struct != FUMFDevSDKPlayerContext::StaticStruct())
	{
		return false;
	}

	FStructOnScope Arguments(Function);
	*Player->ContainerPtrToValuePtr<FUMFDevSDKPlayerContext>(Arguments.GetStructMemory()) = Context;
	Mod.ProcessEvent(Function, Arguments.GetStructMemory());
	Value = Result->GetPropertyValue_InContainer(Arguments.GetStructMemory());
	return true;
}

class FSharedCalls : public IAutomationLatentCommand
{
public:
	explicit FSharedCalls(FAutomationTestBase& InTest) : Test(InTest)
	{
	}

	bool Update() override
	{
		if (FPlatformTime::Seconds() > Deadline)
		{
			Test.AddError(bCalled ? TEXT("TestMod did not unmount in time.")
			                      : TEXT("Both Mod instances and a local pawn must be active."));
			return true;
		}
		for (const FWorldContext& Entry : GEngine->GetWorldContexts())
		{
			UWorld* World = Entry.World();
			UGameInstance* Game = World ? World->GetGameInstance() : nullptr;
			ULocalPlayer* Player = Game ? Game->GetFirstGamePlayer() : nullptr;
			APlayerController* Controller = Player ? Player->GetPlayerController(World) : nullptr;
			AGameCharacter* Pawn = Controller ? Cast<AGameCharacter>(Controller->GetPawn()) : nullptr;
			if (!Pawn)
			{
				continue;
			}
			UModInstance* Second = FindInstance(Game, TEXT("sharedcaller"));
			auto* Host = Game->GetSubsystem<UModAPIHostSubsystem>();
			if (!bCalled)
			{
				UModInstance* First = FindInstance(Game, TEXT("testmod"));
				if (!First || !Second || !Host)
				{
					continue;
				}
				// The host binds once before delivering independent caller tokens.
				auto Query = reinterpret_cast<decltype(&UMF_UnrealModFrameworkDev_ExportTest_GetHostAPIV1)>(
				    UModInstance::ResolveHostAPI("ExportTest", "UMF_UnrealModFrameworkDev_ExportTest_GetHostAPIV1"));
				UMF_UnrealModFrameworkDev_ExportTest_FHostAPIV1 Table{};
				if (!Test.TestTrue(TEXT("Real host query rejects a different ABI hash"),
				                   Query && Query(1, sizeof(Table), "", &Table) != 0) ||
				    !Test.TestEqual(TEXT("Bind published ABI to live game"),
				                    UUMFDevSDKLibrary::ConnectHost(&UModInstance::ResolveHostAPI), int64(0)))
				{
					return true;
				}
				const auto A =
				    Host->OpenLocalPlayer<UGameLocalPlayerAPI>(Player, {TEXT("WorldRead"), TEXT("WorldWrite")});
				const auto B = Host->OpenLocalPlayer<UGameLocalPlayerAPI>(Player, {TEXT("WorldRead")});
				if (!Test.TestTrue(TEXT("Independent callers"),
				                   A.IsSuccess() && B.IsSuccess() &&
				                       A.GetValue().Native.Context != B.GetValue().Native.Context))
				{
					return true;
				}
				const auto& FirstTokens = A.GetValue().Native;
				FirstContext = FUMFDevSDKPlayerContext::FromNative(FirstTokens.Context, FirstTokens.Scope);
				const auto& SecondTokens = B.GetValue().Native;
				SecondContext = FUMFDevSDKPlayerContext::FromNative(SecondTokens.Context, SecondTokens.Scope);
				const auto FirstCaller = A.GetValue().Caller;
				BeforeUnload = Mod::Loader::IModLoaderModule::Get().OnBeforeUnload().AddLambda(
				    [this, Host, FirstCaller](const Mod::Loader::FMountedPackage& Package)
				    {
					    if (Package.ModId == TEXT("testmod"))
					    {
						    bRevoked = Host->RevokeCaller(FirstCaller).IsSuccess();
					    }
				    });
				SecondCaller = B.GetValue().Caller;
				SecondInstance = Second;
				Runtime = FModuleManager::Get().GetModule(TEXT("UMFDevSDKRuntime"));
				bool Accepted = false;
				Test.TestTrue(TEXT("TestMod sets Manny through the SDK"),
				              CallMod<bool, FBoolProperty>(*First, TEXT("ReplaceCharacterMeshWithManny"), FirstContext,
				                                           Accepted) &&
				                  Accepted);
				const FString Mesh = GetPathNameSafe(Pawn->GetMesh()->GetSkeletalMeshAsset());
				Test.TestTrue(TEXT("Pawn uses TestMod's cooked mesh"),
				              Mesh.StartsWith(TEXT("/TestMod/")) && Mesh.EndsWith(TEXT("SKM_Manny_Simple")));
				Pawn->RestoreSaveHealth(73);
				float Health = -1;
				Test.TestTrue(TEXT("SharedCaller reads 73 while both Mods are mounted"),
				              CallMod<float, FFloatProperty>(*Second, TEXT("ReadHealth"), SecondContext, Health) &&
				                  Health == 73);
				Pawn->RestoreMesh();
				UE_LOG(LogTemp, Display, TEXT("UMF_SHARED_BOTH_CALLED mesh=%s health=%.0f ABI=%hs"), *Mesh, Health,
				       UE_UMF_UNREALMODFRAMEWORKDEV_ABI_HASH);
				bCalled = true;
				return false;
			}
			if (!bRevoked || FPackageName::MountPointExists(TEXT("/TestMod/")))
			{
				return false;
			}
			const auto Mounted = Mod::Loader::IModLoaderModule::Get().GetMountedPackages();
			Test.TestTrue(
			    TEXT("Only the second Mod remains mounted"),
			    !Mounted.ContainsByPredicate([](const auto& P) { return P.ModId == TEXT("testmod"); }) &&
			        Mounted.ContainsByPredicate([](const auto& P) { return P.ModId == TEXT("sharedcaller"); }));
			Test.TestTrue(TEXT("Shared Runtime and surviving Mod are the same instances"),
			              Runtime && Runtime == FModuleManager::Get().GetModule(TEXT("UMFDevSDKRuntime")) && Second &&
			                  Second == SecondInstance.Get());
			Pawn->RestoreSaveHealth(41);
			float Health = -1;
			Test.TestTrue(TEXT("Same B caller reads changed pawn health after A unloads"),
			              Second &&
			                  CallMod<float, FFloatProperty>(*Second, TEXT("ReadHealth"), SecondContext, Health) &&
			                  Health == 41);
			float Expired = 0;
			Test.TestTrue(TEXT("A's revoked caller cannot read the same pawn"),
			              Second &&
			                  CallMod<float, FFloatProperty>(*Second, TEXT("ReadHealth"), FirstContext, Expired) &&
			                  Expired == -1);
			Host->RevokeCaller(SecondCaller);
			UE_LOG(LogTemp, Display, TEXT("UMF_SHARED_AFTER_UNLOAD health=%.0f revoked=%.0f"), Health, Expired);
			return true;
		}
		return false;
	}

	~FSharedCalls()
	{
		if (BeforeUnload.IsValid())
		{
			Mod::Loader::IModLoaderModule::Get().OnBeforeUnload().Remove(BeforeUnload);
		}
	}

private:
	FAutomationTestBase& Test;
	const double Deadline = FPlatformTime::Seconds() + 120;
	TWeakObjectPtr<UModInstance> SecondInstance;
	IModuleInterface* Runtime = nullptr;
	Mod::API::FCaller SecondCaller;
	FUMFDevSDKPlayerContext FirstContext;
	FUMFDevSDKPlayerContext SecondContext;
	FDelegateHandle BeforeUnload;
	bool bCalled = false;
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
