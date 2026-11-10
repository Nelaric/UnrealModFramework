// Copyright (c) 2026 Nelaric Contributors
#include "GameModAPIHost.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#if !MOD_API_BOOTSTRAP
#include "ExportTest.modexports.h"
#endif
#include "GameLocalPlayerAPI.h"

void UGameModAPIHost::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if MOD_API_BOOTSTRAP
	UE_LOG(LogTemp, Warning, TEXT("Generated native Mod API bindings are disabled for this build."));
#else
	Registry = MakeUnique<Mod::API::FRegistry>();
	auto Registered = TModResult<TArray<Mod::API::FRegistration>>::Failure(Mod::API::EError::NotRegistered);
	UMF_ModHost_ExportTest(*Registry, Registered);
	if (!Registered.IsSuccess())
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot register generated Mod APIs: %d"),
		       static_cast<int32>(Registered.GetError()));
		Registry.Reset();
		return;
	}
	Types = MoveTemp(Registered.GetValue());
#endif
}

void UGameModAPIHost::Deinitialize()
{
	Providers.Reset();
	Worlds.Reset();
	Types.Reset();
	Registry.Reset();
	Super::Deinitialize();
}

TModResult<FGameModAPICaller> UGameModAPIHost::OpenLocalPlayer(ULocalPlayer* Player, TSet<FName> Permissions)
{
	using namespace Mod::API;
#if MOD_API_BOOTSTRAP
	return TModResult<FGameModAPICaller>::Failure(EError::NotRegistered);
#else
	if (!IsInGameThread())
	{
		return TModResult<FGameModAPICaller>::Failure(EError::WrongThread);
	}
	UWorld* World = Player ? Player->GetWorld() : nullptr;
	if (!Registry || !IsValid(Player) || Player->GetGameInstance() != GetGameInstance() || !IsValid(World))
	{
		return TModResult<FGameModAPICaller>::Failure(EError::ObjectUnavailable);
	}
	UGameLocalPlayerAPI* Service = Player->GetSubsystem<UGameLocalPlayerAPI>();
	if (!Service)
	{
		return TModResult<FGameModAPICaller>::Failure(EError::NotRegistered);
	}
	FPlayerProvider* Provider = Providers.FindByPredicate(
	    [Player, World](const auto& Entry) { return Entry.Player.Get() == Player && Entry.World.Get() == World; });
	if (!Provider)
	{
		const TWeakObjectPtr<UWorld> WorldKey(World);
		if (!Worlds.Contains(WorldKey))
		{
			auto Scope = Registry->OpenWorldScope(World);
			if (!Scope.IsSuccess())
			{
				return TModResult<FGameModAPICaller>::Failure(Scope.GetError());
			}
			Worlds.Add(WorldKey, MoveTemp(Scope.GetValue()));
		}
		auto Scope = Registry->OpenPlayerScope(Player, Worlds.FindChecked(WorldKey));
		if (!Scope.IsSuccess())
		{
			return TModResult<FGameModAPICaller>::Failure(Scope.GetError());
		}
		auto Registration =
		    Registry->RegisterProvider(TBindingTraits<UGameLocalPlayerAPI>::TypeId(), Scope.GetValue(), Service);
		if (!Registration.IsSuccess())
		{
			Registry->CloseScope(Scope.GetValue());
			return TModResult<FGameModAPICaller>::Failure(Registration.GetError());
		}
		FPlayerProvider Entry;
		Entry.Player = Player;
		Entry.World = World;
		Entry.Scope = MoveTemp(Scope.GetValue());
		Entry.Registration = MoveTemp(Registration.GetValue());
		Provider = &Providers.Add_GetRef(MoveTemp(Entry));
	}
	auto Caller = Registry->CreateCaller(Provider->Scope, MoveTemp(Permissions));
	if (!Caller.IsSuccess())
	{
		return TModResult<FGameModAPICaller>::Failure(Caller.GetError());
	}
	auto Binding = Registry->BindProvider(TBindingTraits<UGameLocalPlayerAPI>::TypeId(),
	                                      TBindingTraits<UGameLocalPlayerAPI>::Signature, Caller.GetValue());
	if (!Binding.IsSuccess())
	{
		Registry->RevokeCaller(Caller.GetValue());
		return TModResult<FGameModAPICaller>::Failure(Binding.GetError());
	}
	auto Native = FNativeAccess::Open(*Registry, Caller.GetValue());
	if (!Native.IsSuccess())
	{
		Registry->RevokeCaller(Caller.GetValue());
		return TModResult<FGameModAPICaller>::Failure(Native.GetError());
	}
	return TModResult<FGameModAPICaller>::Success(
	    {MoveTemp(Caller.GetValue()), MoveTemp(Binding.GetValue()), Native.GetValue()});
#endif
}

TModResult<void> UGameModAPIHost::RevokeCaller(const Mod::API::FCaller& Caller)
{
	return Registry ? Registry->RevokeCaller(Caller) : TModResult<void>::Failure(Mod::API::EError::BindingExpired);
}

void UGameModAPIHost::CloseLocalPlayer(ULocalPlayer* Player)
{
	check(IsInGameThread());
	for (int32 Index = Providers.Num() - 1; Index >= 0; --Index)
	{
		if (Providers[Index].Player.Get() == Player)
		{
			if (Registry)
			{
				Registry->CloseScope(Providers[Index].Scope);
			}
			Providers.RemoveAt(Index);
		}
	}
}
