// Copyright (c) 2026 Nelaric Contributors
#include "GameModApiHost.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#if !MOD_API_BOOTSTRAP
#include "ExportTest.modexports.h"
#endif
#include "GameLocalPlayerApi.h"

void UGameModApiHost::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if MOD_API_BOOTSTRAP
	UE_LOG(LogTemp, Warning, TEXT("MHT bootstrap: generated Mod API bindings are unavailable."));
#else
	Registry = MakeUnique<Mod::Api::FRegistry>();
	auto Registered = TModResult<TArray<Mod::Api::FRegistration>>::Failure(Mod::Api::EError::NotRegistered);
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

void UGameModApiHost::Deinitialize()
{
	Providers.Reset();
	Worlds.Reset();
	Types.Reset();
	Registry.Reset();
	Super::Deinitialize();
}

TModResult<FGameModApiCaller> UGameModApiHost::OpenLocalPlayer(ULocalPlayer* Player, TSet<FName> Permissions)
{
	using namespace Mod::Api;
#if MOD_API_BOOTSTRAP
	return TModResult<FGameModApiCaller>::Failure(EError::NotRegistered);
#else
	if (!IsInGameThread())
	{
		return TModResult<FGameModApiCaller>::Failure(EError::WrongThread);
	}
	UWorld* World = Player ? Player->GetWorld() : nullptr;
	if (!Registry || !IsValid(Player) || Player->GetGameInstance() != GetGameInstance() || !IsValid(World))
	{
		return TModResult<FGameModApiCaller>::Failure(EError::ObjectUnavailable);
	}
	UGameLocalPlayerApi* Service = Player->GetSubsystem<UGameLocalPlayerApi>();
	if (!Service)
	{
		return TModResult<FGameModApiCaller>::Failure(EError::NotRegistered);
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
				return TModResult<FGameModApiCaller>::Failure(Scope.GetError());
			}
			Worlds.Add(WorldKey, MoveTemp(Scope.GetValue()));
		}
		auto Scope = Registry->OpenPlayerScope(Player, Worlds.FindChecked(WorldKey));
		if (!Scope.IsSuccess())
		{
			return TModResult<FGameModApiCaller>::Failure(Scope.GetError());
		}
		auto Registration =
		    Registry->RegisterProvider(TBindingTraits<UGameLocalPlayerApi>::TypeId(), Scope.GetValue(), Service);
		if (!Registration.IsSuccess())
		{
			Registry->CloseScope(Scope.GetValue());
			return TModResult<FGameModApiCaller>::Failure(Registration.GetError());
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
		return TModResult<FGameModApiCaller>::Failure(Caller.GetError());
	}
	auto Binding = Registry->BindProvider(TBindingTraits<UGameLocalPlayerApi>::TypeId(),
	                                      TBindingTraits<UGameLocalPlayerApi>::Signature, Caller.GetValue());
	if (!Binding.IsSuccess())
	{
		Registry->RevokeCaller(Caller.GetValue());
		return TModResult<FGameModApiCaller>::Failure(Binding.GetError());
	}
	auto Native = FNativeAccess::Open(*Registry, Caller.GetValue());
	if (!Native.IsSuccess())
	{
		Registry->RevokeCaller(Caller.GetValue());
		return TModResult<FGameModApiCaller>::Failure(Native.GetError());
	}
	return TModResult<FGameModApiCaller>::Success(
	    {MoveTemp(Caller.GetValue()), MoveTemp(Binding.GetValue()), Native.GetValue()});
#endif
}

TModResult<void> UGameModApiHost::RevokeCaller(const Mod::Api::FCaller& Caller)
{
	return Registry ? Registry->RevokeCaller(Caller) : TModResult<void>::Failure(Mod::Api::EError::BindingExpired);
}

void UGameModApiHost::CloseLocalPlayer(ULocalPlayer* Player)
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
