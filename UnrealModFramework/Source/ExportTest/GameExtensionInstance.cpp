// Copyright (c) 2026 Nelaric Contributors
#include "GameExtensionInstance.h"

#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/PendingNetGame.h"
#include "Engine/World.h"
#include "GameExtensionMode.h"
#include "ModExtensionSubsystem.h"

void UGameExtensionInstance::Init()
{
	Super::Init();
	ActorsHandle = FWorldDelegates::OnWorldInitializedActors.AddUObject(this, &UGameExtensionInstance::PrepareClient);
}

void UGameExtensionInstance::Shutdown()
{
	FWorldDelegates::OnWorldInitializedActors.Remove(ActorsHandle);
	Super::Shutdown();
}

FString UGameExtensionInstance::GetModLoginOption()
{
	if (const FWorldContext* Context = GetWorldContext(); Context && Context->PendingNetGame)
	{
		ResetOutgoingPlan();
	}
	const UWorld* World = GetWorld();
	return Mod::Extension::FModNetworkHandshake::MakeLoginOption(World ? World->GetSubsystem<UModExtensionSubsystem>()
	                                                                   : nullptr);
}

void UGameExtensionInstance::ResetOutgoingPlan()
{
	bPlanRequired = false;
	Handshake.Reset();
	ClientPlan.Reset();
	ClientFailure.Reset();
}

void UGameExtensionInstance::HandleGameNetControlMessage(UNetConnection* Connection, uint8 MessageByte,
                                                         const FString& Message)
{
	const FWorldContext* Context = GetWorldContext();
	UPendingNetGame* Pending = Context ? Context->PendingNetGame : nullptr;
	if (Pending && Connection && Pending->NetDriver && Pending->NetDriver->ServerConnection == Connection)
	{
		FString Error;
		if (Handshake.ReceivePlan(*Connection, MessageByte, Message, Error))
		{
			bPlanRequired = true;
			if (!Error.IsEmpty())
			{
				Pending->ConnectionError = MoveTemp(Error);
			}
			return;
		}
	}
	UWorld* World = GetWorld();
	if (Connection && World && Connection->GetDriver() == World->GetNetDriver())
	{
		if (auto* Mode = World->GetAuthGameMode<AGameExtensionMode>();
		    Mode && Mode->HandleModControlMessage(*Connection, MessageByte, Message))
		{
			return;
		}
	}
	Super::HandleGameNetControlMessage(Connection, MessageByte, Message);
}

bool UGameExtensionInstance::DelayPendingNetGameTravel()
{
	const FWorldContext* Context = GetWorldContext();
	UPendingNetGame* Pending = Context ? Context->PendingNetGame : nullptr;
	if (!bPlanRequired)
	{
		const TCHAR* Mode = Pending ? Pending->URL.GetOption(TEXT("game="), TEXT("")) : TEXT("");
		bPlanRequired = FCString::Stricmp(Mode, TEXT("/Script/ExportTest.GameExtensionMode")) == 0;
		if (!bPlanRequired)
		{
			return Super::DelayPendingNetGameTravel();
		}
	}
	UNetConnection* Server = Pending && Pending->NetDriver ? Pending->NetDriver->ServerConnection.Get() : nullptr;
	if (!Server || !Handshake.GetExpectedPlan(*Server, Pending->URL, ClientPlan, ClientFailure))
	{
		if (Pending)
		{
			Pending->ConnectionError = ClientFailure.IsEmpty() ? TEXT("UMF_SERVER_PLAN_REQUIRED") : ClientFailure;
		}
		return true;
	}
	return false;
}

void UGameExtensionInstance::PrepareClient(const FActorsInitializedParams& Params)
{
	UWorld* World = Params.World;
	if (ClientPlan.IsEmpty() || !World || World->GetGameInstance() != this || World->GetNetMode() != NM_Client)
	{
		return;
	}
	if (!PrepareGameExtensions(*World, ClientPlan, ClientFailure) && ClientFailure.IsEmpty())
	{
		ClientFailure = TEXT("UMF_CLIENT_WORLD_NOT_READY");
	}
}

bool UGameExtensionInstance::DelayCompletionOfPendingNetGameTravel()
{
	if (!bPlanRequired)
	{
		return Super::DelayCompletionOfPendingNetGameTravel();
	}
	const FWorldContext* Context = GetWorldContext();
	UPendingNetGame* Pending = Context ? Context->PendingNetGame : nullptr;
	UNetConnection* Server = Pending && Pending->NetDriver ? Pending->NetDriver->ServerConnection.Get() : nullptr;
	const UWorld* World = GetWorld();
	const auto* Extensions = World ? World->GetSubsystem<UModExtensionSubsystem>() : nullptr;
	if (!Server || !Extensions || !ClientFailure.IsEmpty() ||
	    !Mod::Extension::FModNetworkHandshake::ConfirmPlan(*Extensions, *Server, ClientPlan))
	{
		if (Pending)
		{
			Pending->ConnectionError = ClientFailure.IsEmpty() ? TEXT("UMF_CLIENT_PLAN_MISMATCH") : ClientFailure;
		}
		return true;
	}
	// The reliable control channel sends this confirmation before the engine's Join.
	ResetOutgoingPlan();
	return false;
}

FString UGameExtensionLocalPlayer::GetGameLoginOptions() const
{
	auto* Instance = Cast<UGameExtensionInstance>(GetGameInstance());
	return Instance ? Instance->GetModLoginOption() : Super::GetGameLoginOptions();
}
