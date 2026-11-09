// Copyright (c) 2026 Nelaric Contributors
#include "ModApiRegistry.h"

#include "ModApiHost.h"
#include "ModApiNative.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"

namespace Mod::Api
{
struct FRegistrationState
{
	bool bActive = true;
};

struct FScopeState
{
	bool bActive = true;
	EScope Kind = EScope::World;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<ULocalPlayer> Player;
	TSharedPtr<FScopeState> Parent;
	TWeakPtr<FRegistryState> Registry;
};

struct FCallerState
{
	bool bActive = true;
	TSharedPtr<FScopeState> Scope;
	TSet<FName> Permissions;
	TWeakPtr<FRegistryState> Registry;
};

struct FTypeState : FRegistrationState
{
	FTypeDescriptor Descriptor;
	TSet<FName> ExportIds;
};

struct FProviderState : FRegistrationState
{
	TSharedPtr<FScopeState> Scope;
	TSharedPtr<FTypeState> Type;
	TWeakObjectPtr<UObject> Object;
};

struct FRegistryState
{
	bool bActive = true;
	TMap<FName, TSharedPtr<FTypeState>> Types;
	TArray<TSharedPtr<FProviderState>> Providers;
	TArray<TSharedPtr<FScopeState>> Scopes;
	FDelegateHandle CleanupHandle;
};

struct FBindingState
{
	TSharedPtr<FRegistryState> Registry;
	TSharedPtr<FCallerState> Caller;
	TSharedPtr<FScopeState> Scope;
	TSharedPtr<FTypeState> Type;
	TSharedPtr<FProviderState> Provider;
	TWeakObjectPtr<UObject> Object;
	bool bReadOnly = false;
};

namespace
{
struct FNativeState
{
	TSharedPtr<FRegistryState> Registry;
	TSharedPtr<FCallerState> Caller;
	uint64 Scope = 0;
	TMap<uint64, FObjectBinding> Objects;
	TMap<FName, FObjectBinding> Providers;
};

TMap<uint64, FNativeState> NativeContexts;
uint64 NextNativeToken = 1;

uint64 AllocateNativeToken()
{
	// Tokens are never recycled, including across registry lifetimes.
	return NextNativeToken == MAX_uint64 ? 0 : NextNativeToken++;
}

bool ScopeAlive(const TSharedPtr<FScopeState>& Scope)
{
	return Scope && Scope->bActive && Scope->World.IsValid() &&
	       (Scope->Kind == EScope::World || (Scope->Player.IsValid() && ScopeAlive(Scope->Parent)));
}

bool OwnsScope(const TSharedPtr<FRegistryState>& Registry, const TSharedPtr<FScopeState>& Scope)
{
	return Registry && Registry->bActive && ScopeAlive(Scope) && Scope->Registry.Pin() == Registry;
}

bool CallerAlive(const TSharedPtr<FRegistryState>& Registry, const TSharedPtr<FCallerState>& Caller)
{
	return Caller && Caller->bActive && Caller->Registry.Pin() == Registry && OwnsScope(Registry, Caller->Scope);
}

void PruneNativeContexts()
{
	for (auto It = NativeContexts.CreateIterator(); It; ++It)
	{
		if (!CallerAlive(It.Value().Registry, It.Value().Caller))
		{
			It.RemoveCurrent();
		}
	}
}

FNativeState* FindNativeContext(uint64 Context)
{
	FNativeState* Entry = NativeContexts.Find(Context);
	if (Entry && !CallerAlive(Entry->Registry, Entry->Caller))
	{
		NativeContexts.Remove(Context);
		return nullptr;
	}
	return Entry;
}

EError CheckObject(UObject* Object, const TSharedPtr<FScopeState>& Scope)
{
	if (!IsValid(Object))
	{
		return EError::ObjectUnavailable;
	}
	if (Scope->Kind == EScope::Player)
	{
		ULocalPlayer* Player = Scope->Player.Get();
		APlayerController* Controller = Player ? Player->GetPlayerController(Scope->World.Get()) : nullptr;
		if (!IsValid(Controller))
		{
			return EError::ObjectUnavailable;
		}
		if (Object->GetTypedOuter<ULocalPlayer>() != Player || Controller->GetWorld() != Scope->World.Get())
		{
			return EError::ScopeMismatch;
		}
	}
	else if (Object->GetWorld() != Scope->World.Get())
	{
		return EError::ScopeMismatch;
	}
	if (const AActor* Actor = Cast<AActor>(Object);
	    Actor && (Actor->IsActorBeingDestroyed() || !Actor->HasActorBegunPlay()))
	{
		return EError::ObjectUnavailable;
	}
	return EError::None;
}

EError CheckBinding(const TSharedPtr<FBindingState>& Binding)
{
	if (!IsInGameThread())
	{
		return EError::WrongThread;
	}
	if (!Binding || !Binding->Registry->bActive || !CallerAlive(Binding->Registry, Binding->Caller) ||
	    !OwnsScope(Binding->Registry, Binding->Scope) || !Binding->Type->bActive ||
	    (Binding->Provider && !Binding->Provider->bActive))
	{
		return EError::BindingExpired;
	}
	if (Binding->Scope->World != Binding->Caller->Scope->World ||
	    (Binding->Scope->Kind == EScope::Player && Binding->Scope != Binding->Caller->Scope))
	{
		return EError::ScopeMismatch;
	}
	if (!Binding->Object.IsValid() || !Binding->Object->IsA(Binding->Type->Descriptor.Class))
	{
		return EError::ObjectUnavailable;
	}
	return CheckObject(Binding->Object.Get(), Binding->Scope);
}

TSharedPtr<FBindingState> MakeBinding(const TSharedPtr<FRegistryState>& Registry,
                                      const TSharedPtr<FCallerState>& Caller, const TSharedPtr<FScopeState>& Scope,
                                      const TSharedPtr<FTypeState>& Type, UObject* Object)
{
	TSharedPtr<FBindingState> Result = MakeShared<FBindingState>();
	Result->Registry = Registry;
	Result->Caller = Caller;
	Result->Scope = Scope;
	Result->Type = Type;
	Result->Object = Object;
	return Result;
}
} // namespace

FRegistration::~FRegistration()
{
	Reset();
}

FRegistration::FRegistration(FRegistration&& Other) noexcept : State(MoveTemp(Other.State))
{
}

FRegistration& FRegistration::operator=(FRegistration&& Other) noexcept
{
	if (this != &Other)
	{
		Reset();
		State = MoveTemp(Other.State);
	}
	return *this;
}

void FRegistration::Reset()
{
	if (State)
	{
		check(IsInGameThread());
		State->bActive = false;
		State.Reset();
	}
}

FRegistry::FRegistry() : State(MakeShared<FRegistryState>())
{
	check(IsInGameThread());
	const TWeakPtr<FRegistryState> WeakState = State;
	State->CleanupHandle = FWorldDelegates::OnWorldCleanup.AddLambda(
	    [WeakState](UWorld* World, bool, bool)
	    {
		    if (const TSharedPtr<FRegistryState> Pinned = WeakState.Pin())
		    {
			    for (const TSharedPtr<FScopeState>& Scope : Pinned->Scopes)
			    {
				    if (Scope->World.Get() == World)
				    {
					    Scope->bActive = false;
				    }
			    }
			    PruneNativeContexts();
		    }
	    });
}

FRegistry::~FRegistry()
{
	check(IsInGameThread());
	State->bActive = false;
	PruneNativeContexts();
	FWorldDelegates::OnWorldCleanup.Remove(State->CleanupHandle);
}

TModResult<FRegistration> FRegistry::RegisterType(FTypeDescriptor Descriptor)
{
	if (!IsInGameThread())
	{
		return TModResult<FRegistration>::Failure(EError::WrongThread);
	}
	const TSharedPtr<FTypeState>* Existing = State->Types.Find(Descriptor.TypeId);
	if (Descriptor.TypeId.IsNone() || !Descriptor.Class || !Descriptor.Calls || Descriptor.Signature.IsEmpty() ||
	    (Existing && (*Existing)->bActive))
	{
		return TModResult<FRegistration>::Failure(EError::InvalidArgument);
	}
	TSharedPtr<FTypeState> Type = MakeShared<FTypeState>();
	for (const FApiDescriptor& Api : Descriptor.Apis)
	{
		const FName Id(Api.ExportId);
		if (Id.IsNone() || Type->ExportIds.Contains(Id))
		{
			return TModResult<FRegistration>::Failure(EError::InvalidArgument);
		}
		for (const auto& Pair : State->Types)
		{
			if (Pair.Value->bActive && Pair.Value->ExportIds.Contains(Id))
			{
				return TModResult<FRegistration>::Failure(EError::InvalidArgument);
			}
		}
		Type->ExportIds.Add(Id);
	}
	Type->Descriptor = MoveTemp(Descriptor);
	State->Types.Add(Type->Descriptor.TypeId, Type);
	FRegistration Registration;
	Registration.State = Type;
	return TModResult<FRegistration>::Success(MoveTemp(Registration));
}

TModResult<FScope> FRegistry::OpenWorldScope(UWorld* World)
{
	if (!IsInGameThread())
	{
		return TModResult<FScope>::Failure(EError::WrongThread);
	}
	if (!IsValid(World))
	{
		return TModResult<FScope>::Failure(EError::InvalidArgument);
	}
	State->Scopes.RemoveAll([](const auto& Scope) { return !ScopeAlive(Scope); });
	for (const auto& Scope : State->Scopes)
	{
		if (Scope->Kind == EScope::World && Scope->World.Get() == World)
		{
			return TModResult<FScope>::Failure(EError::InvalidArgument);
		}
	}
	FScope Result;
	Result.State = MakeShared<FScopeState>();
	Result.State->World = World;
	Result.State->Registry = State;
	State->Scopes.Add(Result.State);
	return TModResult<FScope>::Success(MoveTemp(Result));
}

TModResult<FScope> FRegistry::OpenPlayerScope(ULocalPlayer* Player, const FScope& WorldScope)
{
	if (!IsInGameThread())
	{
		return TModResult<FScope>::Failure(EError::WrongThread);
	}
	if (!OwnsScope(State, WorldScope.State) || WorldScope.State->Kind != EScope::World || !IsValid(Player))
	{
		return TModResult<FScope>::Failure(EError::ScopeMismatch);
	}
	for (const auto& Scope : State->Scopes)
	{
		if (ScopeAlive(Scope) && Scope->Kind == EScope::Player && Scope->Player.Get() == Player &&
		    Scope->Parent == WorldScope.State)
		{
			return TModResult<FScope>::Failure(EError::InvalidArgument);
		}
	}
	FScope Result;
	Result.State = MakeShared<FScopeState>();
	Result.State->Kind = EScope::Player;
	Result.State->World = WorldScope.State->World;
	Result.State->Player = Player;
	Result.State->Parent = WorldScope.State;
	Result.State->Registry = State;
	State->Scopes.Add(Result.State);
	return TModResult<FScope>::Success(MoveTemp(Result));
}

TModResult<void> FRegistry::CloseScope(const FScope& Scope)
{
	if (!IsInGameThread())
	{
		return TModResult<void>::Failure(EError::WrongThread);
	}
	if (!Scope.State || Scope.State->Registry.Pin() != State)
	{
		return TModResult<void>::Failure(EError::InvalidArgument);
	}
	Scope.State->bActive = false;
	PruneNativeContexts();
	return TModResult<void>::Success();
}

TModResult<FCaller> FRegistry::CreateCaller(const FScope& Scope, TSet<FName> Permissions)
{
	if (!IsInGameThread())
	{
		return TModResult<FCaller>::Failure(EError::WrongThread);
	}
	if (!OwnsScope(State, Scope.State))
	{
		return TModResult<FCaller>::Failure(EError::BindingExpired);
	}
	FCaller Caller;
	Caller.State = MakeShared<FCallerState>();
	Caller.State->Scope = Scope.State;
	Caller.State->Permissions = MoveTemp(Permissions);
	Caller.State->Registry = State;
	return TModResult<FCaller>::Success(MoveTemp(Caller));
}

TModResult<void> FRegistry::RevokeCaller(const FCaller& Caller)
{
	if (!IsInGameThread())
	{
		return TModResult<void>::Failure(EError::WrongThread);
	}
	if (!Caller.State || Caller.State->Registry.Pin() != State)
	{
		return TModResult<void>::Failure(EError::InvalidArgument);
	}
	Caller.State->bActive = false;
	PruneNativeContexts();
	return TModResult<void>::Success();
}

TModResult<FRegistration> FRegistry::RegisterProvider(FName TypeId, const FScope& Scope, UObject* Instance)
{
	if (!IsInGameThread())
	{
		return TModResult<FRegistration>::Failure(EError::WrongThread);
	}
	const TSharedPtr<FTypeState>* Type = State->Types.Find(TypeId);
	if (!Type || !(*Type)->bActive)
	{
		return TModResult<FRegistration>::Failure(EError::NotRegistered);
	}
	if (!OwnsScope(State, Scope.State) || (*Type)->Descriptor.Scope != Scope.State->Kind || !IsValid(Instance) ||
	    !Instance->IsA((*Type)->Descriptor.Class))
	{
		return TModResult<FRegistration>::Failure(EError::ScopeMismatch);
	}
	if (const EError Error = CheckObject(Instance, Scope.State); Error != EError::None)
	{
		return TModResult<FRegistration>::Failure(Error);
	}
	State->Providers.RemoveAll(
	    [](const auto& Provider)
	    { return !Provider->bActive || !Provider->Type->bActive || !ScopeAlive(Provider->Scope); });
	for (const auto& Provider : State->Providers)
	{
		if (Provider->Type == *Type && Provider->Scope == Scope.State)
		{
			return TModResult<FRegistration>::Failure(EError::InvalidArgument);
		}
	}
	TSharedPtr<FProviderState> Provider = MakeShared<FProviderState>();
	Provider->Scope = Scope.State;
	Provider->Type = *Type;
	Provider->Object = Instance;
	State->Providers.Add(Provider);
	FRegistration Registration;
	Registration.State = Provider;
	return TModResult<FRegistration>::Success(MoveTemp(Registration));
}

TModResult<FObjectBinding> FRegistry::BindProvider(FName TypeId, const TCHAR* Signature, const FCaller& Caller)
{
	if (!IsInGameThread())
	{
		return TModResult<FObjectBinding>::Failure(EError::WrongThread);
	}
	if (!CallerAlive(State, Caller.State))
	{
		return TModResult<FObjectBinding>::Failure(EError::BindingExpired);
	}
	const TSharedPtr<FTypeState>* Type = State->Types.Find(TypeId);
	if (!Type || !(*Type)->bActive)
	{
		return TModResult<FObjectBinding>::Failure(EError::NotRegistered);
	}
	if ((*Type)->Descriptor.Signature != Signature)
	{
		return TModResult<FObjectBinding>::Failure(EError::SignatureMismatch);
	}
	const TSharedPtr<FScopeState> TargetScope =
	    (*Type)->Descriptor.Scope == EScope::World && Caller.State->Scope->Kind == EScope::Player
	        ? Caller.State->Scope->Parent
	        : Caller.State->Scope;
	for (const auto& Provider : State->Providers)
	{
		if (Provider->bActive && Provider->Type == *Type && Provider->Scope == TargetScope)
		{
			FObjectBinding Binding;
			Binding.State = MakeBinding(State, Caller.State, TargetScope, *Type, Provider->Object.Get());
			Binding.State->Provider = Provider;
			const EError Error = CheckBinding(Binding.State);
			return Error == EError::None ? TModResult<FObjectBinding>::Success(MoveTemp(Binding))
			                             : TModResult<FObjectBinding>::Failure(Error);
		}
	}
	return TModResult<FObjectBinding>::Failure(EError::NotRegistered);
}

TModResult<const void*> FObjectBinding::ResolveCalls(FName TypeId, const TCHAR* Signature) const
{
	if (const EError Error = CheckBinding(State); Error != EError::None)
	{
		return TModResult<const void*>::Failure(Error);
	}
	if (State->Type->Descriptor.TypeId != TypeId || State->Type->Descriptor.Signature != Signature)
	{
		return TModResult<const void*>::Failure(EError::SignatureMismatch);
	}
	return TModResult<const void*>::Success(State->Type->Descriptor.Calls);
}

TModResult<UObject*> FHostAccess::Enter(const FObjectBinding& Binding, FName TypeId, EScope Scope,
                                        TConstArrayView<FName> Permissions, bool bMutable)
{
	if (const EError Error = CheckBinding(Binding.State); Error != EError::None)
	{
		return TModResult<UObject*>::Failure(Error);
	}
	const auto& Target = Binding.State;
	if (Target->Type->Descriptor.TypeId != TypeId || Target->Scope->Kind != Scope)
	{
		return TModResult<UObject*>::Failure(EError::ScopeMismatch);
	}
	if (bMutable && Target->bReadOnly)
	{
		return TModResult<UObject*>::Failure(EError::ReadOnly);
	}
	for (FName Permission : Permissions)
	{
		if (!Target->Caller->Permissions.Contains(Permission))
		{
			return TModResult<UObject*>::Failure(EError::PermissionDenied);
		}
	}
	return TModResult<UObject*>::Success(Target->Object.Get());
}

TModResult<FObjectBinding> FHostAccess::ProjectObject(const FObjectBinding& Source, UObject* Object, FName TypeId,
                                                      const TCHAR* Signature, bool bReadOnly)
{
	if (const EError Error = CheckBinding(Source.State); Error != EError::None)
	{
		return TModResult<FObjectBinding>::Failure(Error);
	}
	if (!IsValid(Object))
	{
		return TModResult<FObjectBinding>::Failure(EError::ObjectUnavailable);
	}
	const auto& Registry = Source.State->Registry;
	const TSharedPtr<FTypeState>* Type = Registry->Types.Find(TypeId);
	if (!Type || !(*Type)->bActive)
	{
		return TModResult<FObjectBinding>::Failure(EError::NotRegistered);
	}
	if ((*Type)->Descriptor.Signature != Signature)
	{
		return TModResult<FObjectBinding>::Failure(EError::SignatureMismatch);
	}
	if ((*Type)->Descriptor.Scope != EScope::World || !Object->IsA((*Type)->Descriptor.Class))
	{
		return TModResult<FObjectBinding>::Failure(EError::ScopeMismatch);
	}
	const TSharedPtr<FScopeState> WorldScope =
	    Source.State->Scope->Kind == EScope::Player ? Source.State->Scope->Parent : Source.State->Scope;
	if (const EError Error = CheckObject(Object, WorldScope); Error != EError::None)
	{
		return TModResult<FObjectBinding>::Failure(Error);
	}
	FObjectBinding Result;
	Result.State = MakeBinding(Registry, Source.State->Caller, WorldScope, *Type, Object);
	Result.State->bReadOnly = bReadOnly;
	return TModResult<FObjectBinding>::Success(MoveTemp(Result));
}

TModResult<FNativeContext> FNativeAccess::Open(FRegistry& Registry, const FCaller& Caller)
{
	if (!IsInGameThread())
	{
		return TModResult<FNativeContext>::Failure(EError::WrongThread);
	}
	PruneNativeContexts();
	if (!CallerAlive(Registry.State, Caller.State))
	{
		return TModResult<FNativeContext>::Failure(EError::BindingExpired);
	}
	const uint64 Context = AllocateNativeToken();
	const uint64 Scope = AllocateNativeToken();
	if (!Context || !Scope)
	{
		return TModResult<FNativeContext>::Failure(EError::InvalidArgument);
	}
	FNativeState Entry;
	Entry.Registry = Registry.State;
	Entry.Caller = Caller.State;
	Entry.Scope = Scope;
	NativeContexts.Add(Context, MoveTemp(Entry));
	return TModResult<FNativeContext>::Success({Context, Scope});
}

TModResult<void> FNativeAccess::Close(uint64 Context)
{
	if (!IsInGameThread())
	{
		return TModResult<void>::Failure(EError::WrongThread);
	}
	return NativeContexts.Remove(Context) ? TModResult<void>::Success()
	                                      : TModResult<void>::Failure(EError::BindingExpired);
}

TModResult<FObjectBinding> FNativeAccess::Resolve(uint64 Context, uint64 Target, FName TypeId, const TCHAR* Signature)
{
	if (!IsInGameThread())
	{
		return TModResult<FObjectBinding>::Failure(EError::WrongThread);
	}
	FNativeState* Entry = FindNativeContext(Context);
	if (!Entry || !Target)
	{
		return TModResult<FObjectBinding>::Failure(EError::BindingExpired);
	}
	FObjectBinding* Binding = nullptr;
	if (Target == Entry->Scope)
	{
		Binding = Entry->Providers.Find(TypeId);
		if (!Binding)
		{
			const auto* Type = Entry->Registry->Types.Find(TypeId);
			if (!Type || !(*Type)->bActive)
			{
				return TModResult<FObjectBinding>::Failure(EError::NotRegistered);
			}
			const auto Scope =
			    (*Type)->Descriptor.Scope == EScope::World && Entry->Caller->Scope->Kind == EScope::Player
			        ? Entry->Caller->Scope->Parent
			        : Entry->Caller->Scope;
			for (const auto& Provider : Entry->Registry->Providers)
			{
				if (Provider->bActive && Provider->Type == *Type && Provider->Scope == Scope)
				{
					FObjectBinding Value;
					Value.State = MakeBinding(Entry->Registry, Entry->Caller, Scope, *Type, Provider->Object.Get());
					Value.State->Provider = Provider;
					Binding = &Entry->Providers.Add(TypeId, MoveTemp(Value));
					break;
				}
			}
		}
	}
	else
	{
		Binding = Entry->Objects.Find(Target);
	}
	if (!Binding)
	{
		return TModResult<FObjectBinding>::Failure(EError::NotRegistered);
	}
	auto Calls = Binding->ResolveCalls(TypeId, Signature);
	return Calls.IsSuccess() ? TModResult<FObjectBinding>::Success(*Binding)
	                         : TModResult<FObjectBinding>::Failure(Calls.GetError());
}

TModResult<uint64> FNativeAccess::Publish(uint64 Context, FObjectBinding Binding)
{
	if (!IsInGameThread())
	{
		return TModResult<uint64>::Failure(EError::WrongThread);
	}
	FNativeState* Entry = FindNativeContext(Context);
	if (!Entry || !Binding.State || Binding.State->Caller != Entry->Caller ||
	    Binding.State->Registry != Entry->Registry)
	{
		return TModResult<uint64>::Failure(EError::ScopeMismatch);
	}
	if (const EError Error = CheckBinding(Binding.State); Error != EError::None)
	{
		return TModResult<uint64>::Failure(Error);
	}
	for (auto It = Entry->Objects.CreateIterator(); It; ++It)
	{
		const auto& Existing = It.Value().State;
		if (CheckBinding(Existing) != EError::None)
		{
			It.RemoveCurrent();
		}
		else if (Existing->Object == Binding.State->Object && Existing->Type == Binding.State->Type &&
		         Existing->Scope == Binding.State->Scope && Existing->bReadOnly == Binding.State->bReadOnly)
		{
			return TModResult<uint64>::Success(It.Key());
		}
	}
	const uint64 Token = AllocateNativeToken();
	if (!Token)
	{
		return TModResult<uint64>::Failure(EError::InvalidArgument);
	}
	Entry->Objects.Add(Token, MoveTemp(Binding));
	return TModResult<uint64>::Success(Token);
}

TModResult<void> FNativeAccess::Release(uint64 Context, uint64 Object)
{
	if (!IsInGameThread())
	{
		return TModResult<void>::Failure(EError::WrongThread);
	}
	FNativeState* Entry = FindNativeContext(Context);
	return Entry && Entry->Objects.Remove(Object) ? TModResult<void>::Success()
	                                              : TModResult<void>::Failure(EError::BindingExpired);
}
} // namespace Mod::Api
