// Copyright (c) 2026 Nelaric Contributors
#include "ModAPIHostSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "ModAPIHost.h"

DEFINE_LOG_CATEGORY_STATIC(LogModAPIHost, Log, All);

using namespace Mod::API;

void UModAPIHostSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Registry = MakeUnique<FRegistry>();
	auto Registered = FGeneratedHostRegistration::RegisterAll(*Registry);
	if (!Registered.IsSuccess())
	{
		UE_LOG(LogModAPIHost, Error, TEXT("Cannot register generated Mod APIs: %d"),
		       static_cast<int32>(Registered.GetError()));
		Registry.Reset();
		return;
	}
	Types = MoveTemp(Registered.GetValue());
	UE_LOG(LogModAPIHost, Verbose, TEXT("Registered %d generated Mod API types."), Types.Num());

	GetGameInstance()->OnLocalPlayerRemovedEvent.AddUObject(this, &UModAPIHostSubsystem::CloseLocalPlayer);
	FWorldDelegates::OnWorldCleanup.AddUObject(this, &UModAPIHostSubsystem::OnWorldCleanup);
}

void UModAPIHostSubsystem::Deinitialize()
{
	GetGameInstance()->OnLocalPlayerRemovedEvent.RemoveAll(this);
	FWorldDelegates::OnWorldCleanup.RemoveAll(this);
	for (const auto& Entry : Players)
	{
		ReleasePlayerScope(Entry.Key, Entry.Value);
	}
	Players.Reset();
	Worlds.Reset();
	Types.Reset();
	Registry.Reset();
	Super::Deinitialize();
}

TModResult<UModAPIHostSubsystem::FPlayerScope*> UModAPIHostSubsystem::OpenPlayerScope(ULocalPlayer* Player,
                                                                                      UWorld* World)
{
	if (auto* Existing = Players.Find(Player))
	{
		if (Existing->World.Get() == World)
		{
			return TModResult<FPlayerScope*>::Success(Existing);
		}
		CloseLocalPlayer(Player);
	}
	auto* WorldScope = Worlds.Find(World);
	if (!WorldScope)
	{
		auto Opened = Registry->OpenWorldScope(World);
		if (!Opened.IsSuccess())
		{
			return TModResult<FPlayerScope*>::Failure(Opened.GetError());
		}
		WorldScope = &Worlds.Add(World, MoveTemp(Opened.GetValue()));
	}
	auto Opened = Registry->OpenPlayerScope(Player, *WorldScope);
	if (!Opened.IsSuccess())
	{
		return TModResult<FPlayerScope*>::Failure(Opened.GetError());
	}
	auto& Entry = Players.Add(Player);
	Entry.World = World;
	Entry.Scope = MoveTemp(Opened.GetValue());
	Entry.ControllerChanged = Player->OnPlayerControllerChanged().AddWeakLambda(this, [this, Player](APlayerController*)
	                                                                            { CloseLocalPlayer(Player); });
	return TModResult<FPlayerScope*>::Success(&Entry);
}

TModResult<FLocalPlayerCaller> UModAPIHostSubsystem::OpenLocalPlayer(ULocalPlayer* Player, FName TypeId,
                                                                     const TCHAR* Signature, TSet<FName> Permissions)
{
	if (!IsInGameThread())
	{
		return TModResult<FLocalPlayerCaller>::Failure(EError::WrongThread);
	}
	if (!Registry)
	{
		return TModResult<FLocalPlayerCaller>::Failure(EError::NotRegistered);
	}
	if (TypeId.IsNone() || !Signature || !*Signature)
	{
		return TModResult<FLocalPlayerCaller>::Failure(EError::InvalidArgument);
	}
	UWorld* World = IsValid(Player) ? Player->GetWorld() : nullptr;
	if (!IsValid(World) || Player->GetGameInstance() != GetGameInstance() ||
	    !GetGameInstance()->GetLocalPlayers().Contains(Player))
	{
		return TModResult<FLocalPlayerCaller>::Failure(EError::ObjectUnavailable);
	}
	auto Opened = OpenPlayerScope(Player, World);
	if (!Opened.IsSuccess())
	{
		return TModResult<FLocalPlayerCaller>::Failure(Opened.GetError());
	}
	FPlayerScope& Entry = *Opened.GetValue();
	if (!Entry.Providers.Contains(TypeId))
	{
		auto Provider = Registry->RegisterLocalPlayerProvider(TypeId, Entry.Scope);
		if (!Provider.IsSuccess())
		{
			if (Entry.Providers.IsEmpty())
			{
				CloseLocalPlayer(Player);
			}
			return TModResult<FLocalPlayerCaller>::Failure(Provider.GetError());
		}
		Entry.Providers.Add(TypeId, MoveTemp(Provider.GetValue()));
	}
	auto Caller = Registry->CreateCaller(Entry.Scope, MoveTemp(Permissions));
	if (!Caller.IsSuccess())
	{
		return TModResult<FLocalPlayerCaller>::Failure(Caller.GetError());
	}
	auto Binding = Registry->BindProvider(TypeId, Signature, Caller.GetValue());
	if (!Binding.IsSuccess())
	{
		Registry->RevokeCaller(Caller.GetValue());
		return TModResult<FLocalPlayerCaller>::Failure(Binding.GetError());
	}
	auto Native = FNativeAccess::Open(*Registry, Caller.GetValue());
	if (!Native.IsSuccess())
	{
		Registry->RevokeCaller(Caller.GetValue());
		return TModResult<FLocalPlayerCaller>::Failure(Native.GetError());
	}
	return TModResult<FLocalPlayerCaller>::Success(
	    {MoveTemp(Caller.GetValue()), MoveTemp(Binding.GetValue()), Native.GetValue()});
}

TModResult<void> UModAPIHostSubsystem::RevokeCaller(const FCaller& Caller)
{
	if (!IsInGameThread())
	{
		return TModResult<void>::Failure(EError::WrongThread);
	}
	return Registry ? Registry->RevokeCaller(Caller) : TModResult<void>::Failure(EError::BindingExpired);
}

void UModAPIHostSubsystem::CloseLocalPlayer(ULocalPlayer* Player)
{
	if (const auto* Entry = Players.Find(Player))
	{
		ReleasePlayerScope(Player, *Entry);
		Players.Remove(Player);
	}
}

void UModAPIHostSubsystem::OnWorldCleanup(UWorld* World, bool, bool)
{
	Worlds.Remove(World);
	const TWeakObjectPtr<UWorld> WorldKey(World);
	for (auto It = Players.CreateIterator(); It; ++It)
	{
		if (It.Value().World.HasSameIndexAndSerialNumber(WorldKey))
		{
			ReleasePlayerScope(It.Key(), It.Value());
			It.RemoveCurrent();
		}
	}
}

void UModAPIHostSubsystem::ReleasePlayerScope(const TWeakObjectPtr<ULocalPlayer>& Player, const FPlayerScope& Entry)
{
	if (ULocalPlayer* LivePlayer = Player.Get())
	{
		LivePlayer->OnPlayerControllerChanged().Remove(Entry.ControllerChanged);
	}
	Registry->CloseScope(Entry.Scope);
}
