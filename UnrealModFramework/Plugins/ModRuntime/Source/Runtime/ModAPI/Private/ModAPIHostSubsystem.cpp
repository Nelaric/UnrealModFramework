// Copyright (c) 2026 Nelaric Contributors
#include "ModAPIHostSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "ModAPIHost.h"
#include "ModInstance.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectGlobals.h"

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
	SetWorldCallerPolicy({});
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
	auto WorldScope = FindOrOpenWorldScope(World);
	if (!WorldScope.IsSuccess())
	{
		return TModResult<FPlayerScope*>::Failure(WorldScope.GetError());
	}
	auto Opened = Registry->OpenPlayerScope(Player, WorldScope.GetValue());
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

TModResult<FScope> UModAPIHostSubsystem::FindOrOpenWorldScope(UWorld* World)
{
	if (!IsInGameThread())
	{
		return TModResult<FScope>::Failure(EError::WrongThread);
	}
	if (!Registry)
	{
		return TModResult<FScope>::Failure(EError::NotRegistered);
	}
	if (!IsValid(World) || World->GetGameInstance() != GetGameInstance() || !World->IsGameWorld() ||
	    World->bIsTearingDown)
	{
		return TModResult<FScope>::Failure(EError::ScopeMismatch);
	}
	if (const auto* Existing = Worlds.Find(World))
	{
		return TModResult<FScope>::Success(*Existing);
	}
	auto Opened = Registry->OpenWorldScope(World);
	if (Opened.IsSuccess())
	{
		Worlds.Add(World, Opened.GetValue());
	}
	return Opened;
}

TModResult<FRegistration> UModAPIHostSubsystem::RegisterWorldProvider(FName TypeId, UObject* Instance)
{
	if (!IsInGameThread())
	{
		return TModResult<FRegistration>::Failure(EError::WrongThread);
	}
	auto Scope = FindOrOpenWorldScope(IsValid(Instance) ? Instance->GetWorld() : nullptr);
	return Scope.IsSuccess() ? Registry->RegisterProvider(TypeId, Scope.GetValue(), Instance)
	                         : TModResult<FRegistration>::Failure(Scope.GetError());
}

TModResult<FWorldCaller> UModAPIHostSubsystem::OpenWorldCaller(UWorld* World, FName TypeId, const TCHAR* Signature,
                                                               TSet<FName> Permissions)
{
	using FResult = TModResult<FWorldCaller>;
	auto Scope = FindOrOpenWorldScope(World);
	if (!Scope.IsSuccess())
	{
		return FResult::Failure(Scope.GetError());
	}
	if (TypeId.IsNone() || !Signature || !*Signature)
	{
		return FResult::Failure(EError::InvalidArgument);
	}
	auto Caller = Registry->CreateCaller(Scope.GetValue(), MoveTemp(Permissions));
	if (!Caller.IsSuccess())
	{
		return FResult::Failure(Caller.GetError());
	}
	auto Binding = Registry->BindProvider(TypeId, Signature, Caller.GetValue());
	if (!Binding.IsSuccess())
	{
		Registry->RevokeCaller(Caller.GetValue());
		return FResult::Failure(Binding.GetError());
	}
	auto Native = FNativeAccess::Open(*Registry, Caller.GetValue());
	if (!Native.IsSuccess())
	{
		Registry->RevokeCaller(Caller.GetValue());
		return FResult::Failure(Native.GetError());
	}
	auto Object = FNativeAccess::Publish(Native.GetValue().Context, MoveTemp(Binding.GetValue()));
	if (!Object.IsSuccess())
	{
		Registry->RevokeCaller(Caller.GetValue());
		return FResult::Failure(Object.GetError());
	}
	return FResult::Success({MoveTemp(Caller.GetValue()), Native.GetValue(), Object.GetValue()});
}

void UModAPIHostSubsystem::SetWorldCallerPolicy(FAuthorizeWorldCaller Policy)
{
	check(IsInGameThread());
	for (const auto& Mod : ModCallers)
	{
		for (const auto& Entry : Mod.Value)
		{
			RevokeCaller(Entry.Value.Caller);
		}
	}
	ModCallers.Reset();
	WorldCallerPolicy.Reset();
	if (Policy.IsBound())
	{
		WorldCallerPolicy = MakeShared<FAuthorizeWorldCaller>(MoveTemp(Policy));
	}
}

void* UModAPIHostSubsystem::ResolveHostQuery(const char* Module, const char* Symbol)
{
#if PLATFORM_WINDOWS && !IS_MONOLITHIC
	if (!IsInGameThread() || !Module || !Symbol)
	{
		return nullptr;
	}
	auto& Modules = FModuleManager::Get();
	const FName Name(UTF8_TO_TCHAR(Module));
	if (!Modules.IsModuleLoaded(Name))
	{
		return nullptr;
	}
	const FString Filename = Modules.GetModuleFilename(Name);
	void* Handle = Filename.IsEmpty() ? nullptr : FPlatformProcess::GetDllHandle(*Filename);
	if (!Handle)
	{
		return nullptr;
	}
	void* Address = FPlatformProcess::GetDllExport(Handle, UTF8_TO_TCHAR(Symbol));
	FPlatformProcess::FreeDllHandle(Handle);
	return Address;
#else
	(void)Module;
	(void)Symbol;
	return nullptr;
#endif
}

TModResult<FWorldCaller> UModAPIHostSubsystem::AuthorizeWorldCaller(UModInstance& Mod, FName TypeId,
                                                                    const TCHAR* Signature)
{
	using FResult = TModResult<FWorldCaller>;
	if (!IsInGameThread())
	{
		return FResult::Failure(EError::WrongThread);
	}
	const auto Policy = WorldCallerPolicy;
	if (!IsValid(&Mod) || Mod.GetOuter() != GetGameInstance() || Mod.GetModId().IsEmpty() || !Policy ||
	    !Policy->IsBound() || TypeId.IsNone() || !Signature || !*Signature)
	{
		return FResult::Failure(EError::PermissionDenied);
	}
	const TWeakObjectPtr<UModInstance> Key(&Mod);
	if (auto* Existing = ModCallers.Find(Key))
	{
		if (const auto* Caller = Existing->Find(TypeId))
		{
			auto Binding = FNativeAccess::ResolveObject(Caller->Native.Context, Caller->Object, TypeId, Signature);
			if (Binding.IsSuccess())
			{
				return FResult::Success(*Caller);
			}
			if (Binding.GetError() == EError::SignatureMismatch)
			{
				return FResult::Failure(EError::SignatureMismatch);
			}
			RevokeCaller(Caller->Caller);
			Existing->Remove(TypeId);
		}
	}
	auto Grant = Policy->Execute(Mod, TypeId);
	if (!Grant.IsSuccess())
	{
		return FResult::Failure(Grant.GetError());
	}
	if (Policy != WorldCallerPolicy || !IsValid(&Mod) || Mod.GetModId().IsEmpty())
	{
		return FResult::Failure(EError::BindingExpired);
	}
	if (Grant.GetValue().World != Mod.GetWorld())
	{
		return FResult::Failure(EError::ScopeMismatch);
	}
	auto Result = OpenWorldCaller(Grant.GetValue().World, TypeId, Signature, MoveTemp(Grant.GetValue().Permissions));
	if (Result.IsSuccess())
	{
		auto& Callers = ModCallers.FindOrAdd(Key);
		if (const auto* Previous = Callers.Find(TypeId))
		{
			RevokeCaller(Previous->Caller);
		}
		Callers.Add(TypeId, Result.GetValue());
	}
	return Result;
}

void UModAPIHostSubsystem::RevokeModCallers(UModInstance& Mod)
{
	check(IsInGameThread());
	TMap<FName, FWorldCaller> Callers;
	if (ModCallers.RemoveAndCopyValue(&Mod, Callers))
	{
		for (const auto& Entry : Callers)
		{
			RevokeCaller(Entry.Value.Caller);
		}
	}
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
	FScope WorldScope;
	if (Registry && Worlds.RemoveAndCopyValue(World, WorldScope))
	{
		Registry->CloseScope(WorldScope);
	}
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
