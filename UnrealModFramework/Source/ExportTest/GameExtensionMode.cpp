// Copyright (c) 2026 Nelaric Contributors
#include "GameExtensionMode.h"

#include "Engine/SkeletalMesh.h"
#include "GameCharacter.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "ModLoader.h"
#include "ModExtensionSubsystem.h"
#include "ModExtensionSession.h"
#include "ModExtensionSave.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/Package.h"
#include "Engine/NetConnection.h"
#include "Engine/ChildConnection.h"

namespace
{
FString SaveFile(const FString& Slot)
{
	if (Slot.IsEmpty() || Slot.Len() > 64)
	{
		return {};
	}
	for (const TCHAR Ch : Slot)
	{
		if (!FChar::IsAlnum(Ch) && Ch != TEXT('_') && Ch != TEXT('-'))
		{
			return {};
		}
	}
	return FPaths::ProjectSavedDir() / TEXT("ModSaves") / (Slot + TEXT(".umfsave"));
}

Mod::Extension::FModSavePolicy SavePolicy()
{
	Mod::Extension::FModSavePolicy Policy;
	Policy.GameVersion = 2;
	GConfig->GetString(TEXT("ModLoader"), TEXT("BaseReleaseVersion"), Policy.BaseRelease, GGameIni);
	Policy.ExportContract = Mod::Extension::FModNetworkHandshake::GetContractDigest();
	static const Mod::Extension::FModSaveMigration MoveHealth = [](TArray<uint8>& Bytes, FString& Error)
	{
		float Health = 0;
		FMemoryReader Reader(Bytes);
		Reader << Health;
		if (Reader.IsError() || Reader.Tell() != Reader.TotalSize() || !FMath::IsFinite(Health))
		{
			Error = TEXT("The old character health field is invalid.");
			return false;
		}
		TArray<uint8> Updated;
		FMemoryWriter Writer(Updated);
		double PreciseHealth = Health;
		Writer << PreciseHealth;
		Bytes = MoveTemp(Updated);
		return true;
	};
	Policy.FindMigration = [](FStringView Mod, FStringView Block,
	                          uint32 Version) -> const Mod::Extension::FModSaveMigration*
	{ return Mod.IsEmpty() && Block == TEXTVIEW("game") && Version == 1 ? &MoveHealth : nullptr; };
	return Policy;
}
} // namespace

AGameExtensionMode::AGameExtensionMode()
{
	DefaultPawnClass = AGameCharacter::StaticClass();
	bUseSeamlessTravel = false;
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
	Extensions->SetFailureHandler(Mod::Extension::FOnWorldFailure::CreateWeakLambda(
	    this,
	    [this](const Mod::Extension::FWorldFailure& Failure)
	    {
		    if (Failure.bRestartRequired)
		    {
			    FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("ModExtensions"));
		    }
		    else if (UWorld* Current = GetWorld(); Current && Current->HasBegunPlay() && !Current->bIsTearingDown)
		    {
			    Current->GetTimerManager().SetTimerForNextTick(
			        FTimerDelegate::CreateUObject(this, &AGameExtensionMode::ReturnToMainMenuHost));
		    }
	    }));
	FString Error;
	TSharedPtr<Mod::Extension::FModSaveSnapshot> Original;
	TFunction<bool(FString&)> ValidateSave;
	TFunction<bool(FString&)> StageSave;
	if (!RequestedSave.IsEmpty())
	{
		const FString Path = SaveFile(RequestedSave);
		Original = MakeShared<Mod::Extension::FModSaveSnapshot>();
		if (Path.IsEmpty() || GetNetMode() != NM_Standalone ||
		    !Mod::Extension::FModExtensionSave::ReadFile(Path, *Original, Error))
		{
			UE_LOG(LogTemp, Error, TEXT("Could not load Mod save. %s"), *Error);
			Extensions->Stop();
			return;
		}
		ValidateSave = [Extensions, Original](FString& Detail)
		{ return Mod::Extension::FModExtensionSave::Admit(*Extensions, SavePolicy(), *Original, Detail); };
		StageSave = [this, Original](FString& Detail) { return RestorePendingSave(*Original, Detail); };
	}
	if (!PrepareGameExtensions(*World, {}, Error, MoveTemp(ValidateSave), MoveTemp(StageSave)))
	{
		UE_LOG(LogTemp, Error, TEXT("Could not start extension gameplay: %s"), *Error);
		Extensions->Stop();
		return;
	}
	LoadedSave.GameplayDigest = Extensions->GetGameplayDigest();
	Super::StartPlay();
	const auto Players = MoveTemp(WaitingPlayers);
	for (const auto& Controller : Players)
	{
		if (APlayerController* Player = Controller.Get(); Player && Player->GetWorld() == World && !Player->GetPawn())
		{
			Super::HandleStartingNewPlayer_Implementation(Player);
			RestorePlayer(*Player);
		}
	}
}

void AGameExtensionMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	RequestedSave = UGameplayStatics::ParseOption(Options, TEXT("UMFSave"));
}

void AGameExtensionMode::ModWriteSave(const FString& Slot)
{
	using namespace Mod::Extension;
	const auto* Extensions = GetWorld()->GetSubsystem<UModExtensionSubsystem>();
	const auto* Player = GetWorld()->GetFirstPlayerController();
	const auto* Character = Player ? Cast<AGameCharacter>(Player->GetPawn()) : nullptr;
	const FString Path = SaveFile(Slot);
	if (Path.IsEmpty() || !Character || !Extensions || GetNetMode() != NM_Standalone)
	{
		UE_LOG(LogTemp, Error, TEXT("ModWriteSave requires a valid slot and an active standalone character."));
		return;
	}
	FModSaveSnapshot Snapshot = LoadedSave;
	Snapshot.GameData.Reset();
	FMemoryWriter Writer(Snapshot.GameData);
	double Health = Character->GetSaveHealth();
	Writer << Health;
	Writer.Close();
	FString Error;
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true) ||
	    !FModExtensionSave::Capture(*Extensions, SavePolicy(), Snapshot, Error) ||
	    !FModExtensionSave::WriteNewFile(Path, Snapshot, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("Could not write Mod save. %s"), *Error);
	}
}

void AGameExtensionMode::ModLoadSave(const FString& Slot)
{
	if (const FString Path = SaveFile(Slot);
	    GetNetMode() == NM_Standalone && !Path.IsEmpty() && IFileManager::Get().FileExists(*Path))
	{
		const FName Map(*UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()));
		UGameplayStatics::OpenLevel(this, Map, true, TEXT("game=ModExtensions?UMFSave=") + Slot);
	}
}

bool AGameExtensionMode::RestorePendingSave(const Mod::Extension::FModSaveSnapshot& Original, FString& Error)
{
	using namespace Mod::Extension;
	const auto* Extensions = GetWorld()->GetSubsystem<UModExtensionSubsystem>();
	FModSaveSnapshot Ready;
	if (!Extensions || !FModExtensionSave::Migrate(*Extensions, SavePolicy(), Original, Ready, Error))
	{
		return false;
	}
	FMemoryReader Reader(Ready.GameData);
	double Health = 0;
	Reader << Health;
	if (Reader.IsError() || Reader.Tell() != Reader.TotalSize() || !FMath::IsFinite(Health) ||
	    FMath::Abs(Health) > MAX_flt)
	{
		Error = TEXT("The saved character health is invalid.");
		return false;
	}
	SavedHealth = static_cast<float>(Health);
	LoadedSave = MoveTemp(Ready);
	return true;
}

void AGameExtensionMode::RestorePlayer(APlayerController& Player)
{
	if (auto* Character = Cast<AGameCharacter>(Player.GetPawn()); Character && SavedHealth)
	{
		Character->RestoreSaveHealth(SavedHealth.GetValue());
		SavedHealth.Reset();
	}
}

bool PrepareGameExtensions(UWorld& World, const FString& ExpectedPlan, FString& Error,
                           TFunction<bool(FString&)> ValidatePreparation, TFunction<bool(FString&)> StageSave)
{
	auto* Extensions = World.GetSubsystem<UModExtensionSubsystem>();
	if (!Extensions || Extensions->IsReady())
	{
		Error = TEXT("The World cannot start extension preparation.");
		return false;
	}
	Mod::Extension::FContentSlot Meshes;
	Meshes.AssetType = USkeletalMesh::StaticClass();
	Meshes.bAllowReplace = true;
	Meshes.bAllowDisable = true;
	Meshes.Defaults.Add(TEXT("character.mannequin"),
	                    FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
	Extensions->DefineContentSlot(TEXT("character.mesh"), MoveTemp(Meshes));
	Extensions->RegisterAnchor(TEXT("training.target"), *World.PersistentLevel, FTransform(FVector(0, 0, 100)),
	                           TEXT("WorldPatch.training.target"));
	Mod::Extension::FWorldPolicy Policy;
	Policy.ExpectedGameplayDigest = ExpectedPlan;
	for (const auto& Package : Mod::Loader::IModLoaderModule::Get().GetMountedPackages())
	{
		// Sample host policy for the loader-approved local mod set.
		auto& Grants = Policy.Grants.FindOrAdd(Package.ModId);
		Grants = Package.GrantedCapabilities;
		Grants.Append({TEXT("Health.Observe"), TEXT("Health.Change"), TEXT("WorldRead"), TEXT("Content.character.mesh"),
		               TEXT("WorldPatch.training.target"), TEXT("hook/game.character/change-health.ModifyInputs"),
		               TEXT("hook/game.character/change-health.Cancel"),
		               TEXT("hook/game.character/change-health.ModifyResult")});
	}
	Policy.ValidatePreparation = MoveTemp(ValidatePreparation);
	Policy.StageSavedState = MoveTemp(StageSave);
	return Extensions->Prepare(MoveTemp(Policy), Error);
}

void AGameExtensionMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
                                  FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (ErrorMessage.IsEmpty())
	{
		const auto* Extensions = GetWorld()->GetSubsystem<UModExtensionSubsystem>();
		if (!Extensions)
		{
			ErrorMessage = TEXT("UMF_WORLD_NOT_READY");
		}
		else
		{
			Mod::Extension::FModNetworkHandshake::VerifyLogin(*Extensions, Options, ErrorMessage);
		}
	}
}

APlayerController* AGameExtensionMode::Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal,
                                             const FString& Options, const FUniqueNetIdRepl& UniqueId,
                                             FString& ErrorMessage)
{
	if (auto* Connection = Cast<UNetConnection>(NewPlayer))
	{
		const auto* Extensions = GetWorld()->GetSubsystem<UModExtensionSubsystem>();
		if (!Extensions)
		{
			ErrorMessage = TEXT("UMF_WORLD_NOT_READY");
			return nullptr;
		}
		if (!Mod::Extension::FModNetworkHandshake::VerifyLogin(*Extensions, Options, ErrorMessage))
		{
			return nullptr;
		}
		if (auto* Child = Cast<UChildConnection>(Connection))
		{
			Connection = Child->GetParentConnection();
		}
		if (!Connection || !AcknowledgedPeers.Contains(Connection))
		{
			ErrorMessage = TEXT("UMF_PLAN_ACKNOWLEDGMENT_REQUIRED");
			return nullptr;
		}
	}
	return Super::Login(NewPlayer, InRemoteRole, Portal, Options, UniqueId, ErrorMessage);
}

void AGameExtensionMode::GameWelcomePlayer(UNetConnection* Connection, FString& RedirectURL)
{
	Super::GameWelcomePlayer(Connection, RedirectURL);
	if (const auto* Extensions = GetWorld()->GetSubsystem<UModExtensionSubsystem>(); Connection && Extensions)
	{
		Mod::Extension::FModNetworkHandshake::SendPlan(*Extensions, *Connection);
	}
}

bool AGameExtensionMode::HandleModControlMessage(UNetConnection& Connection, uint8 Message, const FString& Value)
{
	const auto* Extensions = GetWorld()->GetSubsystem<UModExtensionSubsystem>();
	bool bAccepted = false;
	if (!Extensions ||
	    !Mod::Extension::FModNetworkHandshake::ReceiveAcknowledgment(*Extensions, Message, Value, bAccepted))
	{
		return false;
	}
	if (Connection.ClientLoginState == EClientLoginState::Welcomed)
	{
		for (auto It = AcknowledgedPeers.CreateIterator(); It; ++It)
		{
			const auto* Peer = It->Get();
			if (!Peer || Peer->GetConnectionState() == USOCK_Closed)
			{
				It.RemoveCurrent();
			}
		}
		if (bAccepted)
		{
			AcknowledgedPeers.Add(&Connection);
		}
		else
		{
			AcknowledgedPeers.Remove(&Connection);
		}
	}
	return true;
}

void AGameExtensionMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	const UWorld* World = GetWorld();
	const auto* Extensions = World ? World->GetSubsystem<UModExtensionSubsystem>() : nullptr;
	if (!Extensions || Extensions->IsReady())
	{
		Super::HandleStartingNewPlayer_Implementation(NewPlayer);
		if (IsValid(NewPlayer))
		{
			RestorePlayer(*NewPlayer);
		}
	}
	else if (IsValid(NewPlayer) && Extensions->GetFailure().Error == Mod::API::EError::None)
	{
		WaitingPlayers.AddUnique(NewPlayer);
	}
}
