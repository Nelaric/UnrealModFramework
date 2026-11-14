// Copyright (c) 2026 Nelaric Contributors
#include "ModExtensionSubsystem.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "ModExtensionWorld.h"
#include "ModInstance.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogModExtensionHost, Log, All);

using namespace Mod::Extension;
using Mod::API::EError;
using FWorldState = FModExtensionWorld;
using EState = FWorldState::EState;

namespace
{
uint64 WorldGeneration = 0;

TSharedPtr<FWorldState> FindState(UWorld* World, FName Id, FEndpoint& Endpoint)
{
	if (World == Endpoint.World.Get() && Id == Endpoint.Id)
	{
		if (auto State = Endpoint.State.Pin())
		{
			return State;
		}
	}
	Endpoint = {};
	return nullptr;
}

bool IsModHandlerLive(const FHandler& Handler, const UWorld* World)
{
	UObject* Target = Handler.Target.Get();
	return Target && !Target->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) && Target->GetWorld() == World;
}

bool ValidOrder(const FHandler& Handler)
{
	return Handler.Order.Before.Num() <= 32 && Handler.Order.After.Num() <= 32 &&
	       !Handler.Order.Before.Contains(Handler.ModId) && !Handler.Order.After.Contains(Handler.ModId);
}

EOperation OperationsFor(EStage Stage)
{
	if (Stage == EStage::Pre)
	{
		return EOperation::ModifyInputs | EOperation::Cancel;
	}
	return Stage == EStage::Post ? EOperation::ModifyResult : EOperation::None;
}

bool SupportsStage(const FDefinition& Definition, EStage Stage)
{
	if (Definition.Kind == EKind::Event)
	{
		return Stage == EStage::Event;
	}
	return (Stage == EStage::Pre && Definition.bPre) || (Stage == EStage::Post && Definition.bPost);
}

FHookResult RunCore(float Value, TFunctionRef<FCoreResult(float)> Core)
{
	const auto Result = Core(Value);
	return {Result.Value, Result.Error, false, Result.bCommitted};
}
} // namespace

bool UModExtensionSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

void UModExtensionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	State = MakeShared<FWorldState>(*GetWorld(), ++WorldGeneration);
	TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &UModExtensionSubsystem::OnWorldTearDown);
	LevelRemovedHandle =
	    FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &UModExtensionSubsystem::OnLevelRemoved);
}

void UModExtensionSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle);
	FWorldDelegates::LevelRemovedFromWorld.Remove(LevelRemovedHandle);
	Stop();
	State.Reset();
	Super::Deinitialize();
}

void UModExtensionSubsystem::OnWorldTearDown(UWorld* World)
{
	if (World == GetWorld())
	{
		Stop();
	}
}

void UModExtensionSubsystem::OnLevelRemoved(ULevel* Level, UWorld* World)
{
	if (!Level || World != GetWorld() || !State)
	{
		return;
	}
	TArray<FName> Removed;
	for (const auto& [Id, Anchor] : State->Anchors)
	{
		if (Anchor.Level.Get() == Level)
		{
			Removed.Add(Id);
		}
	}
	for (const FName Id : Removed)
	{
		RemoveAnchor(Id, *Level);
	}
}

bool UModExtensionSubsystem::Prepare(FWorldPolicy Policy, FString& OutError)
{
	if (!IsInGameThread() || !State || State->Phase != EState::New || GetWorld()->bIsTearingDown)
	{
		OutError = TEXT("The World cannot start extension preparation.");
		return false;
	}
	const auto WorldState = State;
	WorldState->Policy = MoveTemp(Policy);
	WorldState->Phase = EState::Preparing;
	++WorldState->Depth;
	ON_SCOPE_EXIT
	{
		WorldState->FinishDispatch();
	};
	if (!WorldState->Prepare(OutError))
	{
		WorldState->Phase = EState::Draining;
		return false;
	}
	WorldState->Phase = EState::Ready;
	UE_LOG(LogModExtensionHost, Display,
	       TEXT("World extension plan is ready: %d mods, %d channels, %d content IDs, %d patches, %d services."),
	       WorldState->Mods.Num(), WorldState->Channels.Num(), WorldState->Contents.Num(), WorldState->Applied.Num(),
	       WorldState->Services.Num());
	return true;
}

bool UModExtensionSubsystem::IsReady() const
{
	return IsInGameThread() && State && State->Phase == EState::Ready && !GetWorld()->bIsTearingDown;
}

void UModExtensionSubsystem::Stop()
{
	if (IsInGameThread() && State && State->Phase != EState::Closed)
	{
		const auto WorldState = State;
		WorldState->Phase = EState::Draining;
		WorldState->Drain();
	}
}

TModResult<FSubscription> UModExtensionSubsystem::Register(UModInstance& Owner, FName Id, const FString& Signature,
                                                           FHandler Handler)
{
	using FResult = TModResult<FSubscription>;
	if (!IsInGameThread())
	{
		return FResult::Failure(EError::WrongThread);
	}
	if (!State || (State->Phase != EState::Preparing && State->Phase != EState::Ready) || !State->Owns(Owner))
	{
		return FResult::Failure(EError::NotReady);
	}
	const auto Fail = [&](EError Error)
	{
		if (State->Phase == EState::Preparing && State->RegistrationError.IsEmpty())
		{
			State->RegistrationError = FString::Printf(TEXT("%s: extension registration failed for %s (%d)."),
			                                           *Owner.GetModId(), *Id.ToString(), static_cast<int32>(Error));
		}
		return FResult::Failure(Error);
	};
	const auto* Found = State->Channels.Find(Id);
	if (!Found)
	{
		return Fail(EError::NotRegistered);
	}
	const auto& Channel = **Found;
	const FDefinition& Definition = Channel.GetDefinition();
	if (!Signature.IsEmpty() && Definition.Signature != Signature)
	{
		return Fail(EError::SignatureMismatch);
	}
	const auto& Requirements =
	    State->Mods[State->Instances.FindChecked(FObjectKey(&Owner))].Package->Extensions.Contracts;
	if (Signature.IsEmpty() &&
	    !Requirements.ContainsByPredicate([&](const auto& Entry)
	                                      { return Entry.Id == Id && Entry.Signature == Definition.Signature; }))
	{
		return Fail(EError::SignatureMismatch);
	}
	Handler.ModId = Owner.GetModId();
	Handler.Owner = &Owner;
	if (!State->Admit(Definition) || !State->HasGrant(Handler.ModId, Definition.Permission) ||
	    (EnumHasAnyFlags(Handler.Operations, EOperation::ModifyInputs) &&
	     !State->HasGrant(Handler.ModId, Definition.ModifyInputsPermission)) ||
	    (EnumHasAnyFlags(Handler.Operations, EOperation::Cancel) &&
	     !State->HasGrant(Handler.ModId, Definition.CancelPermission)) ||
	    (EnumHasAnyFlags(Handler.Operations, EOperation::ModifyResult) &&
	     !State->HasGrant(Handler.ModId, Definition.ModifyResultPermission)) ||
	    !EnumHasAllFlags(Definition.Operations & OperationsFor(Handler.Stage), Handler.Operations))
	{
		return Fail(EError::PermissionDenied);
	}
	if (!SupportsStage(Definition, Handler.Stage) || Handler.Key.IsNone() || !ValidOrder(Handler) ||
	    !FMath::IsFinite(Handler.CancelValue) || !IsModHandlerLive(Handler, GetWorld()) ||
	    (Handler.Stage == EStage::Event ? !Handler.Event : !Handler.Hook))
	{
		return Fail(EError::InvalidArgument);
	}
	const auto Added = (*Found)->Add(MoveTemp(Handler));
	if (!Added.IsSuccess())
	{
		return Fail(Added.GetError());
	}
	State->bDirty = true;
	if (State->Depth == 0 && IsReady())
	{
		FString Error;
		if (!State->RefreshSnapshots(Error))
		{
			UE_LOG(LogModExtensionHost, Error, TEXT("%s"), *Error);
			return FResult::Failure(EError::Conflict);
		}
	}
	return FResult::Success({Id, State->Generation, Added.GetValue()});
}

void UModExtensionSubsystem::Unregister(UModInstance& Owner, const FSubscription& Token)
{
	if (!IsInGameThread() || !State || State->Generation != Token.Generation || !State->Owns(Owner) ||
	    (State->Phase != EState::Preparing && State->Phase != EState::Ready))
	{
		return;
	}
	if (const auto* Channel = State->Channels.Find(Token.Id))
	{
		(*Channel)->Remove(Owner, Token.Serial);
		State->bDirty = true;
		if (State->Depth == 0 && IsReady())
		{
			FString Error;
			if (!State->RefreshSnapshots(Error))
			{
				UE_LOG(LogModExtensionHost, Error, TEXT("%s"), *Error);
			}
		}
	}
}

TModResult<void> UModExtensionSubsystem::Provide(UModInstance& Owner, const FService& Service, UObject* Instance)
{
	using FResult = TModResult<void>;
	if (!IsInGameThread())
	{
		return FResult::Failure(EError::WrongThread);
	}
	if (!State || !State->APIRegistry || State->Phase != EState::Preparing || State->bBindingServices ||
	    !State->Owns(Owner))
	{
		return FResult::Failure(EError::NotReady);
	}
	const FString ModId = Owner.GetModId();
	const auto Fail = [&](EError Error)
	{
		if (State->RegistrationError.IsEmpty())
		{
			State->RegistrationError = FString::Printf(TEXT("%s: could not provide service %s (%d)."), *ModId,
			                                           *Service.Id.ToString(), static_cast<int32>(Error));
		}
		return FResult::Failure(Error);
	};
	if (Service.Id.IsNone() || Service.TypeId.IsNone() || Service.Signature.IsEmpty() ||
	    !Mod::Loader::FVersionRange{}.Contains(Service.Version) || !IsValid(Instance))
	{
		return Fail(EError::InvalidArgument);
	}
	if (!State->HasGrant(ModId, Service.Permission))
	{
		return Fail(EError::PermissionDenied);
	}
	if (State->Services.Contains(Service.Id))
	{
		return Fail(EError::Conflict);
	}
	auto Registration =
	    State->APIRegistry->RegisterService(Service.Id, Service.TypeId, *Service.Signature, State->APIScope, Instance);
	if (!Registration.IsSuccess())
	{
		return Fail(Registration.GetError());
	}
	auto& Entry = State->Services.Add(Service.Id);
	Entry.Owner = &Owner;
	Entry.ModId = ModId;
	Entry.Definition = Service;
	Entry.Registration = MoveTemp(Registration.GetValue());
	Entry.Target.Reset(Instance);
	return FResult::Success();
}

void UModExtensionSubsystem::Withdraw(UModInstance& Owner, FName Id)
{
	if (!IsInGameThread() || !State || !State->Owns(Owner))
	{
		return;
	}
	if (const auto* Entry = State->Services.Find(Id); Entry && Entry->Owner.Get() == &Owner)
	{
		State->Services.Remove(Id);
	}
}

TModResult<Mod::API::FObjectBinding> UModExtensionSubsystem::Bind(UModInstance& Consumer, FName Id, FName TypeId,
                                                                  const TCHAR* Signature)
{
	using FResult = TModResult<Mod::API::FObjectBinding>;
	if (!IsInGameThread())
	{
		return FResult::Failure(EError::WrongThread);
	}
	if (!State || !State->APIRegistry || !Signature || (State->Phase != EState::Ready && !State->bBindingServices) ||
	    !State->Owns(Consumer))
	{
		return FResult::Failure(EError::NotReady);
	}
	const auto& Package = *State->Mods[State->Instances.FindChecked(FObjectKey(&Consumer))].Package;
	const auto* Need = Package.Extensions.Services.FindByPredicate([Id](const auto& Item) { return Item.Id == Id; });
	const auto* Entry = State->Services.Find(Id);
	if (!Need || !Entry || Entry->ModId != Need->Provider || !Entry->Owner.IsValid())
	{
		return FResult::Failure(EError::NotRegistered);
	}
	if (Need->TypeId != TypeId || Need->Signature != Signature || Entry->Definition.TypeId != TypeId ||
	    Entry->Definition.Signature != Signature || !Need->Versions.Contains(Entry->Definition.Version))
	{
		return FResult::Failure(EError::SignatureMismatch);
	}
	if (!State->HasGrant(Package.ModId, Entry->Definition.Permission))
	{
		return FResult::Failure(EError::PermissionDenied);
	}
	const FObjectKey Key(&Consumer);
	if (!State->Callers.Contains(Key))
	{
		auto Caller = State->APIRegistry->CreateCaller(State->APIScope, State->Policy.Grants.FindRef(Package.ModId));
		if (!Caller.IsSuccess())
		{
			return FResult::Failure(Caller.GetError());
		}
		State->Callers.Add(Key, MoveTemp(Caller.GetValue()));
	}
	return State->APIRegistry->BindService(Id, TypeId, Signature, State->Callers.FindChecked(Key));
}

bool UModExtensionSubsystem::AcquireNativeService(UModInstance& Consumer, FName Id, FName TypeId,
                                                  const TCHAR* Signature, uint64& OutContext, uint64& OutObject)
{
	auto Binding = Bind(Consumer, Id, TypeId, Signature);
	if (!Binding.IsSuccess())
	{
		return false;
	}
	const FObjectKey Key(&Consumer);
	auto* Context = State->NativeCallers.Find(Key);
	if (!Context)
	{
		const auto Open = Mod::API::FNativeAccess::Open(*State->APIRegistry, State->Callers.FindChecked(Key));
		if (!Open.IsSuccess())
		{
			return false;
		}
		Context = &State->NativeCallers.Add(Key, Open.GetValue());
	}
	auto Object = Mod::API::FNativeAccess::Publish(Context->Context, MoveTemp(Binding.GetValue()));
	if (!Object.IsSuccess())
	{
		return false;
	}
	OutContext = Context->Context;
	OutObject = Object.GetValue();
	return true;
}

FHookResult UModExtensionSubsystem::Invoke(UObject& Source, FName Id, FEndpoint& Endpoint, float Value,
                                           TFunctionRef<FCoreResult(float)> Core,
                                           TFunctionRef<bool(float)> ValidateInput,
                                           TFunctionRef<bool(float)> ValidateResult)
{
	if (!IsInGameThread())
	{
		return {0, EError::WrongThread};
	}
	if (!IsValid(&Source))
	{
		return {0, EError::ObjectUnavailable};
	}
	UWorld* World = Source.GetWorld();
	if (World && World->bIsTearingDown)
	{
		return {0, EError::BindingExpired};
	}
	auto WorldState = FindState(World, Id, Endpoint);
	if (!WorldState)
	{
		const auto* Host = World ? World->GetSubsystem<UModExtensionSubsystem>() : nullptr;
		WorldState = Host ? Host->State : nullptr;
	}
	if (WorldState && (WorldState->Phase == EState::Draining || WorldState->Phase == EState::Closed))
	{
		return {0, EError::BindingExpired};
	}
	if (!WorldState || WorldState->Phase == EState::New)
	{
		return RunCore(Value, Core);
	}
	if (WorldState->Phase != EState::Ready)
	{
		return {0, EError::NotReady};
	}
	auto Channel = Endpoint.Channel.Pin();
	if (!Channel)
	{
		Channel = WorldState->Channels.FindRef(Id);
		Endpoint = {World, Channel, WorldState, Id};
	}
	if (!Channel || Channel->GetDefinition().Kind != EKind::Hook)
	{
		return {0, EError::NotRegistered};
	}
	if (!WorldState->Admit(Channel->GetDefinition()))
	{
		return RunCore(Value, Core);
	}
	++WorldState->Depth;
	ON_SCOPE_EXIT
	{
		WorldState->FinishDispatch();
	};
	return Channel->Invoke(Source, Value, Core, ValidateInput, ValidateResult);
}

EError UModExtensionSubsystem::Emit(UObject& Source, FName Id, FEndpoint& Endpoint, float Value)
{
	if (!IsInGameThread())
	{
		return EError::WrongThread;
	}
	UWorld* World = IsValid(&Source) ? Source.GetWorld() : nullptr;
	auto WorldState = FindState(World, Id, Endpoint);
	if (!WorldState)
	{
		const auto* Host = World ? World->GetSubsystem<UModExtensionSubsystem>() : nullptr;
		WorldState = Host ? Host->State : nullptr;
	}
	if (!WorldState || WorldState->Phase != EState::Ready)
	{
		return EError::NotReady;
	}
	auto Channel = Endpoint.Channel.Pin();
	if (!Channel)
	{
		Channel = WorldState->Channels.FindRef(Id);
		Endpoint = {World, Channel, WorldState, Id};
	}
	if (!Channel || Channel->GetDefinition().Kind != EKind::Event)
	{
		return EError::NotRegistered;
	}
	const auto& Definition = Channel->GetDefinition();
	const auto* Mod = Cast<UModInstance>(&Source);
	if (!WorldState->Admit(Definition) ||
	    (Mod && (!WorldState->Owns(*Mod) || Definition.PublishPermission.IsNone() ||
	             !WorldState->HasGrant(Mod->GetModId(), Definition.PublishPermission))))
	{
		return EError::PermissionDenied;
	}
	if (!FMath::IsFinite(Value))
	{
		return EError::InvalidArgument;
	}
	++WorldState->Depth;
	ON_SCOPE_EXIT
	{
		WorldState->FinishDispatch();
	};
	return Channel->Emit(Source, Value);
}
