// Copyright (c) 2026 Nelaric Contributors
#include "ModExtensionSubsystem.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "ModAPIHostSubsystem.h"
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
	if (Stage == EStage::On)
	{
		return EOperation::Wrap | EOperation::Replace;
	}
	return Stage == EStage::Post ? EOperation::ModifyResult : EOperation::None;
}

bool SupportsStage(const FDefinition& Definition, EStage Stage)
{
	if (Definition.Kind == EKind::Event)
	{
		return Stage == EStage::Event;
	}
	return (Stage == EStage::Pre && Definition.bPre) || (Stage == EStage::Post && Definition.bPost) ||
	       (Stage == EStage::On && Definition.bOn);
}

template <class T> THookResult<T> RunCore(T Value, TFunctionRef<TCoreResult<T>(T)> Core)
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
	TickHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &UModExtensionSubsystem::OnWorldTickStart);
	TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &UModExtensionSubsystem::OnWorldTearDown);
	LevelRemovedHandle =
	    FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &UModExtensionSubsystem::OnLevelRemoved);
}

void UModExtensionSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle);
	FWorldDelegates::OnWorldTickStart.Remove(TickHandle);
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

void UModExtensionSubsystem::OnWorldTickStart(UWorld* World, ELevelTick, float)
{
	if (World == GetWorld() && IsReady() && State->Depth == 0)
	{
		const auto WorldState = State;
		FString Error;
		if (Mod::Loader::IModLoaderModule::Get().IsRestartRequired())
		{
			WorldState->Fail(EError::BindingExpired, TEXT("The Mod runtime requires a process restart."), true);
		}
		else if (!WorldState->ValidateServices(Error))
		{
			WorldState->Fail(EError::BindingExpired, MoveTemp(Error));
		}
		else
		{
			WorldState->DispatchQueuedEvents();
		}
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
		WorldState->Fail(EError::NotReady, OutError,
		                 WorldState->bPatchCommitted || Mod::Loader::IModLoaderModule::Get().IsRestartRequired());
		return false;
	}
	WorldState->GameplayDigest = WorldState->CalculateGameplayDigest();
	if (WorldState->Phase != EState::Preparing || WorldState->GameplayDigest.IsEmpty() ||
	    (!WorldState->Policy.ExpectedGameplayDigest.IsEmpty() &&
	     WorldState->Policy.ExpectedGameplayDigest != WorldState->GameplayDigest))
	{
		OutError = TEXT("The prepared World does not match the expected gameplay plan.");
		WorldState->Fail(EError::SignatureMismatch, OutError, WorldState->bPatchCommitted);
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

bool UModExtensionSubsystem::MatchesGameplayDigest(const FString& Digest) const
{
	return IsReady() && !Digest.IsEmpty() && State->GameplayDigest == Digest;
}

FString UModExtensionSubsystem::GetGameplayDigest() const
{
	return IsReady() ? State->GameplayDigest : FString();
}

TMap<FString, FString> UModExtensionSubsystem::GetPackageRevisions() const
{
	TMap<FString, FString> Revisions;
	if (IsReady())
	{
		for (const auto& Mod : State->Mods)
		{
			Revisions.Add(Mod.Package->ModId, Mod.Package->ArchiveSha256);
		}
	}
	return Revisions;
}

FWorldFailure UModExtensionSubsystem::GetFailure() const
{
	return IsInGameThread() && State ? State->Failure : FWorldFailure{};
}

void UModExtensionSubsystem::SetFailureHandler(FOnWorldFailure Handler)
{
	if (IsInGameThread() && State && State->Phase != EState::Closed)
	{
		State->FailureHandler = MoveTemp(Handler);
	}
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
	if (GetWorld()->bIsTearingDown || !State->HasGrant(Handler.ModId, Definition.Permission) ||
	    (EnumHasAnyFlags(Handler.Operations, EOperation::ModifyInputs) &&
	     !State->HasGrant(Handler.ModId, Definition.ModifyInputsPermission)) ||
	    (EnumHasAnyFlags(Handler.Operations, EOperation::Cancel) &&
	     !State->HasGrant(Handler.ModId, Definition.CancelPermission)) ||
	    (EnumHasAnyFlags(Handler.Operations, EOperation::ModifyResult) &&
	     !State->HasGrant(Handler.ModId, Definition.ModifyResultPermission)) ||
	    (EnumHasAnyFlags(Handler.Operations, EOperation::Wrap) &&
	     !State->HasGrant(Handler.ModId, FName(Definition.Id.ToString() + TEXT(".Wrap")))) ||
	    (EnumHasAnyFlags(Handler.Operations, EOperation::Replace) &&
	     !State->HasGrant(Handler.ModId, FName(Definition.Id.ToString() + TEXT(".Replace")))) ||
	    !EnumHasAllFlags(Definition.Operations & OperationsFor(Handler.Stage), Handler.Operations))
	{
		return Fail(EError::PermissionDenied);
	}
	if (!SupportsStage(Definition, Handler.Stage) || Handler.Key.IsNone() || !ValidOrder(Handler) ||
	    !FMath::IsFinite(Handler.CancelValue) || !IsModHandlerLive(Handler, GetWorld()) ||
	    !Handler.UsesScalar(Definition.Scalar) || !Handler.HasCallback())
	{
		return Fail(EError::InvalidArgument);
	}
	if (Definition.Payload.IsObject())
	{
		if (!State->APIRegistry || Definition.Payload.TypeId.IsNone() || Definition.Payload.Signature.IsEmpty())
		{
			return Fail(EError::SignatureMismatch);
		}
		const FObjectKey OwnerKey(&Owner);
		if (!State->Callers.Contains(OwnerKey))
		{
			auto Caller =
			    State->APIRegistry->CreateCaller(State->APIScope, State->Policy.Grants.FindRef(Handler.ModId));
			if (!Caller.IsSuccess())
			{
				return Fail(Caller.GetError());
			}
			State->Callers.Add(OwnerKey, MoveTemp(Caller.GetValue()));
		}
		Handler.Project = [WeakState = TWeakPtr<FWorldState>(State), OwnerKey, Type = Definition.Payload](
		                      const FPayload& Value, const TSharedRef<const uint8>& Lifetime) -> TModResult<FPayload>
		{
			using FProjection = TModResult<FPayload>;
			const auto World = WeakState.Pin();
			if (!World || !World->APIHostOwner.IsValid() || World->APIHostOwner->GetRegistry() != World->APIRegistry ||
			    !World->Callers.Contains(OwnerKey))
			{
				return FProjection::Failure(EError::BindingExpired);
			}
			FPayload Projected = FPayload::FromObject(Value.GetObject());
			if (const auto* Object = Value.GetObject())
			{
				auto Binding = World->APIRegistry->BindPayload(
				    World->Callers.FindChecked(OwnerKey), *const_cast<UObject*>(Object), Type.TypeId, *Type.Signature);
				if (!Binding.IsSuccess())
				{
					return FProjection::Failure(Binding.GetError());
				}
				auto Borrow = Binding.GetValue().Borrow(Lifetime);
				if (!Borrow.IsSuccess())
				{
					return FProjection::Failure(Borrow.GetError());
				}
				Projected.SetView(MoveTemp(Borrow.GetValue()));
			}
			else if (!Value.IsNullObject())
			{
				return FProjection::Failure(EError::ObjectUnavailable);
			}
			return FProjection::Success(MoveTemp(Projected));
		};
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

ESubscriptionStatus UModExtensionSubsystem::GetSubscriptionStatus(const UModInstance& Owner,
                                                                  const FSubscription& Token) const
{
	if (!IsInGameThread() || !State || State->Generation != Token.Generation || !State->Owns(Owner))
	{
		return ESubscriptionStatus::Inactive;
	}
	const auto* Channel = State->Channels.Find(Token.Id);
	if (!Channel)
	{
		return ESubscriptionStatus::Inactive;
	}
	if ((*Channel)->Contains(Owner, Token.Serial, true))
	{
		return ESubscriptionStatus::Active;
	}
	return (*Channel)->Contains(Owner, Token.Serial, false) ? ESubscriptionStatus::Pending
	                                                        : ESubscriptionStatus::Inactive;
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
	const auto& Manifest = State->Mods[State->Instances.FindChecked(FObjectKey(&Owner))].Package->Extensions;
	if (Manifest.SchemaVersion >= 2 && !Manifest.Provides.ContainsByPredicate(
	                                       [&Service](const auto& Offer)
	                                       {
		                                       return Offer.Id == Service.Id && Offer.TypeId == Service.TypeId &&
		                                              Offer.Signature == Service.Signature &&
		                                              Offer.Version == Service.Version &&
		                                              Offer.Permission == Service.Permission && Offer.Patch.IsNone();
	                                       }))
	{
		return Fail(EError::SignatureMismatch);
	}
	if (!State->HasGrant(ModId, Service.Permission))
	{
		return Fail(EError::PermissionDenied);
	}
	if (State->Services.Contains(Service.Id))
	{
		return Fail(EError::Conflict);
	}
	auto Registration = State->APIRegistry->RegisterService(Service.Id, Service.TypeId, *Service.Signature,
	                                                        State->APIScope, Instance, State);
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
	const auto WorldState = State;
	if (const auto* Entry = WorldState->Services.Find(Id); Entry && Entry->Owner.Get() == &Owner)
	{
		WorldState->Services.Remove(Id);
		FString Error;
		if (WorldState->Phase == EState::Ready && !WorldState->ValidateServices(Error))
		{
			WorldState->Fail(EError::BindingExpired, MoveTemp(Error));
		}
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
	if (!State || !State->APIRegistry || !Signature ||
	    (State->Phase != EState::Ready && !State->bBindingServices &&
	     (!State->CleanupOwner || State->CleanupOwner.GetValue() != FObjectKey(&Consumer))) ||
	    !State->Owns(Consumer))
	{
		return FResult::Failure(EError::NotReady);
	}
	const auto& Package = *State->Mods[State->Instances.FindChecked(FObjectKey(&Consumer))].Package;
	const auto* Need = Package.Extensions.Services.FindByPredicate([Id](const auto& Item) { return Item.Id == Id; });
	const auto* Entry = State->Services.Find(Id);
	if (!Need || !Entry || Entry->ModId != Need->Provider ||
	    (Entry->PatchIndex == INDEX_NONE && !Entry->Owner.IsValid()))
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
	return InvokeScalar<float>(Source, Id, Endpoint, Value, Core, ValidateInput, ValidateResult);
}

template <class T>
THookResult<T> UModExtensionSubsystem::InvokeScalar(UObject& Source, FName Id, FEndpoint& Endpoint, T Value,
                                                    TFunctionRef<TCoreResult<T>(T)> Core,
                                                    TFunctionRef<bool(T)> ValidateInput,
                                                    TFunctionRef<bool(T)> ValidateResult)
{
	if (!IsInGameThread())
	{
		return {T{}, EError::WrongThread};
	}
	if (!IsValid(&Source))
	{
		return {T{}, EError::ObjectUnavailable};
	}
	UWorld* World = Source.GetWorld();
	if (World && World->bIsTearingDown)
	{
		return {T{}, EError::BindingExpired};
	}
	auto WorldState = FindState(World, Id, Endpoint);
	if (!WorldState)
	{
		const auto* Host = World ? World->GetSubsystem<UModExtensionSubsystem>() : nullptr;
		WorldState = Host ? Host->State : nullptr;
	}
	if (WorldState && (WorldState->Phase == EState::Draining || WorldState->Phase == EState::Closed))
	{
		return {T{}, EError::BindingExpired};
	}
	if (!WorldState || WorldState->Phase == EState::New)
	{
		return RunCore(Value, Core);
	}
	if (WorldState->Phase != EState::Ready)
	{
		return {T{}, EError::NotReady};
	}
	auto Channel = Endpoint.Channel.Pin();
	if (!Channel)
	{
		Channel = WorldState->Channels.FindRef(Id);
		Endpoint = {World, Channel, WorldState, Id};
	}
	if (!Channel || Channel->GetDefinition().Kind != EKind::Hook)
	{
		return {T{}, EError::NotRegistered};
	}
	if (Channel->GetDefinition().Scalar != ScalarType<T>())
	{
		return {T{}, EError::SignatureMismatch};
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
	if constexpr (std::is_same_v<T, FPayload>)
	{
		const auto& Payload = Channel->GetDefinition().Payload;
		if (!Payload.Matches(Value, World))
		{
			return {T{}, EError::SignatureMismatch};
		}
		return Channel->Invoke<T>(
		    Source, Value, Core, [&](const T& Input) { return Payload.Matches(Input, World) && ValidateInput(Input); },
		    [&](const T& Result) { return Payload.Matches(Result, World) && ValidateResult(Result); });
	}
	return Channel->Invoke<T>(Source, Value, Core, ValidateInput, ValidateResult);
}

EError UModExtensionSubsystem::Emit(UObject& Source, FName Id, FEndpoint& Endpoint, float Value)
{
	return EmitScalar<float>(Source, Id, Endpoint, Value);
}

template <class T> EError UModExtensionSubsystem::EmitScalar(UObject& Source, FName Id, FEndpoint& Endpoint, T Value)
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
	if (Definition.Scalar != ScalarType<T>())
	{
		return EError::SignatureMismatch;
	}
	const auto* Mod = Cast<UModInstance>(&Source);
	if (!WorldState->Admit(Definition) ||
	    (Mod && (!WorldState->Owns(*Mod) || Definition.PublishPermission.IsNone() ||
	             !WorldState->HasGrant(Mod->GetModId(), Definition.PublishPermission))))
	{
		return EError::PermissionDenied;
	}
	if constexpr (std::is_same_v<T, FPayload>)
	{
		if (!Definition.Payload.Matches(Value, World))
		{
			return EError::SignatureMismatch;
		}
	}
	else if constexpr (std::is_floating_point_v<T>)
	{
		if (!FMath::IsFinite(Value))
		{
			return EError::InvalidArgument;
		}
	}
	if (Definition.bQueued)
	{
		constexpr int32 Capacity = 256;
		if (WorldState->QueuedEvents.Num() >= Capacity)
		{
			return EError::QueueFull;
		}
		WorldState->QueuedEvents.Add(
		    [Channel, WeakSource = TWeakObjectPtr<UObject>(&Source), Value]()
		    {
			    if (auto* LiveSource = WeakSource.Get())
			    {
				    if constexpr (std::is_same_v<T, FPayload>)
				    {
					    if (!Channel->GetDefinition().Payload.Matches(Value, LiveSource->GetWorld()))
					    {
						    return;
					    }
				    }
				    Channel->Emit(*LiveSource, Value);
			    }
		    });
		return EError::None;
	}
	++WorldState->Depth;
	ON_SCOPE_EXIT
	{
		WorldState->FinishDispatch();
	};
	return Channel->Emit(Source, Value);
}

#define UMF_INSTANTIATE_EXTENSION(Scalar)                                                                              \
	template MODAPI_API THookResult<Scalar> UModExtensionSubsystem::InvokeScalar(                                      \
	    UObject&, FName, FEndpoint&, Scalar, TFunctionRef<TCoreResult<Scalar>(Scalar)>, TFunctionRef<bool(Scalar)>,    \
	    TFunctionRef<bool(Scalar)>);                                                                                   \
	template MODAPI_API EError UModExtensionSubsystem::EmitScalar(UObject&, FName, FEndpoint&, Scalar)

UMF_INSTANTIATE_EXTENSION(float);
UMF_INSTANTIATE_EXTENSION(double);
UMF_INSTANTIATE_EXTENSION(int32);
UMF_INSTANTIATE_EXTENSION(int64);
UMF_INSTANTIATE_EXTENSION(bool);
UMF_INSTANTIATE_EXTENSION(uint8);
UMF_INSTANTIATE_EXTENSION(FPayload);
#undef UMF_INSTANTIATE_EXTENSION
