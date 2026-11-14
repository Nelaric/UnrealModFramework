// Copyright (c) 2026 Nelaric Contributors
#include "ModExtension.h"

#include "Templates/UnrealTemplate.h"
#include "Misc/ScopeExit.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "ModInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogModExtension, Log, All);

namespace Mod::Extension
{
namespace
{
using API::EError;

bool OrderedBefore(const FHandler& Left, const FHandler& Right)
{
	if (Left.Order.Priority != Right.Order.Priority)
	{
		return Left.Order.Priority < Right.Order.Priority;
	}
	if (Left.ModId != Right.ModId)
	{
		return Left.ModId < Right.ModId;
	}
	return Left.Key.LexicalLess(Right.Key);
}

bool IsLive(const FHandler& Handler)
{
	const auto* Owner = Handler.Owner.Get();
	const auto* Target = Handler.Target.Get();
	if (!Owner || !Target || Owner->GetWorld() != Target->GetWorld())
	{
		return false;
	}
	if (const auto* Actor = Cast<AActor>(Target))
	{
		return !Actor->IsActorBeingDestroyed() && Actor->HasActorBegunPlay();
	}
	if (const auto* Component = Cast<UActorComponent>(Target))
	{
		return Component->IsRegistered() && Component->HasBegunPlay();
	}
	return true;
}

template <class T> THookDecision<T> CallHook(const FHandler& Handler, T Value)
{
	if constexpr (std::is_same_v<T, float>)
	{
		if (Handler.Hook)
		{
			return Handler.Hook(Value);
		}
	}
	if constexpr (std::is_same_v<T, FPayload>)
	{
		if (Handler.Project)
		{
			const TSharedRef<const uint8> Lifetime = MakeShared<uint8>(0);
			auto Projected = Handler.Project(Value, Lifetime);
			if (!Projected.IsSuccess())
			{
				return {{}, Projected.GetError()};
			}
			return Handler.GetTypedCalls<T>()->Hook(MoveTemp(Projected.GetValue()));
		}
	}
	return Handler.GetTypedCalls<T>()->Hook(Value);
}

template <class T> EError CallEvent(const FHandler& Handler, T Value)
{
	if constexpr (std::is_same_v<T, float>)
	{
		if (Handler.Event)
		{
			return Handler.Event(Value);
		}
	}
	if constexpr (std::is_same_v<T, FPayload>)
	{
		if (Handler.Project)
		{
			const TSharedRef<const uint8> Lifetime = MakeShared<uint8>(0);
			auto Projected = Handler.Project(Value, Lifetime);
			return Projected.IsSuccess() ? Handler.GetTypedCalls<T>()->Event(MoveTemp(Projected.GetValue()))
			                             : Projected.GetError();
		}
	}
	return Handler.GetTypedCalls<T>()->Event(Value);
}
} // namespace

bool SortExtensions(TConstArrayView<FHandler> Handlers, TArray<int32>& Out, FString& Detail)
{
	Out.Reset();
	TArray<TArray<int32>> Edges;
	TArray<int32> Incoming;
	Edges.SetNum(Handlers.Num());
	Incoming.Init(0, Handlers.Num());
	for (int32 From = 0; From < Handlers.Num(); ++From)
	{
		for (int32 To = 0; To < Handlers.Num(); ++To)
		{
			if (From != To && (Handlers[From].Order.Before.Contains(Handlers[To].ModId) ||
			                   Handlers[To].Order.After.Contains(Handlers[From].ModId)))
			{
				Edges[From].Add(To);
				++Incoming[To];
			}
		}
	}
	while (Out.Num() < Handlers.Num())
	{
		int32 Best = INDEX_NONE;
		for (int32 Index = 0; Index < Handlers.Num(); ++Index)
		{
			if (Incoming[Index] == 0 && (Best == INDEX_NONE || OrderedBefore(Handlers[Index], Handlers[Best])))
			{
				Best = Index;
			}
		}
		if (Best == INDEX_NONE)
		{
			TArray<FString> Cycle;
			for (int32 Index = 0; Index < Handlers.Num(); ++Index)
			{
				if (Incoming[Index] > 0)
				{
					Cycle.Add(Handlers[Index].ModId + TEXT("/") + Handlers[Index].Key.ToString());
				}
			}
			Cycle.Sort();
			Detail = TEXT("Unsatisfied ordering constraints: ") + FString::Join(Cycle, TEXT(", "));
			return false;
		}
		Out.Add(Best);
		Incoming[Best] = -1;
		for (const int32 Index : Edges[Best])
		{
			--Incoming[Index];
		}
	}
	return true;
}

TModResult<uint64> FChannel::Add(FHandler Handler)
{
	using FResult = TModResult<uint64>;
	for (const auto& Existing : Desired)
	{
		if (Existing->ModId == Handler.ModId && Existing->Key == Handler.Key && Existing->Stage == Handler.Stage)
		{
			const bool bSame =
			    Existing->Owner == Handler.Owner && Existing->Target == Handler.Target &&
			    Existing->Identity == Handler.Identity && Existing->Function == Handler.Function &&
			    Existing->Operations == Handler.Operations && Existing->CancelValue == Handler.CancelValue &&
			    Existing->Order.Priority == Handler.Order.Priority && Existing->Order.Before == Handler.Order.Before &&
			    Existing->Order.After == Handler.Order.After;
			return bSame && (Handler.Identity || !Handler.Function.IsNone())
			           ? FResult::Success(Existing->Serial)
			           : FResult::Failure(EError::InvalidArgument);
		}
	}
	if (Desired.Num() >= 256 || LastSerial == MAX_uint64)
	{
		return FResult::Failure(EError::InvalidArgument);
	}
	Handler.Serial = ++LastSerial;
	Desired.Add(MakeShared<FHandler>(MoveTemp(Handler)));
	return FResult::Success(LastSerial);
}

void FChannel::Remove(UModInstance& Owner, uint64 Serial)
{
	const auto Matches = [&Owner, Serial](const auto& Handler)
	{ return Handler->Serial == Serial && Handler->Owner.Get() == &Owner; };
	if (Definition.bImmediateUnsubscribe && Events && Events->ContainsByPredicate(Matches))
	{
		ImmediatelyRemoved.Add(Serial);
	}
	Desired.RemoveAll(Matches);
}

bool FChannel::Build(FString& Detail)
{
	Desired.RemoveAll([](const auto& Handler) { return !Handler->Owner.IsValid() || !Handler->Target.IsValid(); });
	const auto SortStage = [&](EStage Stage, TSharedPtr<const FSnapshot>& Destination)
	{
		TArray<FHandler> Order;
		FSnapshot Candidates;
		for (const auto& Handler : Desired)
		{
			if (Handler->Stage == Stage)
			{
				FHandler& Entry = Order.AddDefaulted_GetRef();
				Entry.ModId = Handler->ModId;
				Entry.Key = Handler->Key;
				Entry.Order = Handler->Order;
				Candidates.Add(Handler);
			}
		}
		TArray<int32> Sorted;
		if (!SortExtensions(Order, Sorted, Detail))
		{
			Detail = Definition.Id.ToString() + TEXT(": ") + Detail;
			return false;
		}
		auto Snapshot = MakeShared<FSnapshot>();
		Snapshot->Reserve(Sorted.Num());
		for (int32 Index : Sorted)
		{
			Snapshot->Add(Candidates[Index]);
		}
		Destination = MoveTemp(Snapshot);
		return true;
	};
	if (!SortStage(EStage::Pre, PendingPre) || !SortStage(EStage::Post, PendingPost) ||
	    !SortStage(EStage::Event, PendingEvents) || !SortStage(EStage::On, PendingOn))
	{
		return false;
	}
	TArray<FString> Replacements;
	for (const auto& Handler : *PendingOn)
	{
		if (Handler->Operations == EOperation::Replace)
		{
			Replacements.Add(Handler->ModId + TEXT("/") + Handler->Key.ToString());
		}
	}
	if (Replacements.Num() > 1)
	{
		Detail = Definition.Id.ToString() + TEXT(": competing Hook replacements: ") +
		         FString::Join(Replacements, TEXT(", "));
		return false;
	}
	return true;
}

void FChannel::Publish()
{
	Pre = MoveTemp(PendingPre);
	Post = MoveTemp(PendingPost);
	On = MoveTemp(PendingOn);
	Events = MoveTemp(PendingEvents);
	ImmediatelyRemoved.Reset();
}

void FChannel::Discard()
{
	PendingPre.Reset();
	PendingPost.Reset();
	PendingOn.Reset();
	PendingEvents.Reset();
	if (!ImmediatelyRemoved.IsEmpty() && Events)
	{
		auto Live = MakeShared<FSnapshot>(*Events);
		Live->RemoveAll([this](const auto& Handler) { return ImmediatelyRemoved.Contains(Handler->Serial); });
		Events = MoveTemp(Live);
		ImmediatelyRemoved.Reset();
	}
	Desired.Reset();
	for (const auto* Snapshot : {&Pre, &On, &Post, &Events})
	{
		if (*Snapshot)
		{
			Desired.Append(**Snapshot);
		}
	}
}

bool FChannel::Contains(const UModInstance& Owner, uint64 Serial, bool bPublished) const
{
	const auto Match = [&Owner, Serial](const auto& Entry)
	{ return Entry->Serial == Serial && Entry->Owner.Get() == &Owner; };
	if (ImmediatelyRemoved.Contains(Serial))
	{
		return false;
	}
	if (!bPublished)
	{
		return Desired.ContainsByPredicate(Match);
	}
	for (const auto* Phase : {&Pre, &On, &Post, &Events})
	{
		if (*Phase && (*Phase)->ContainsByPredicate(Match))
		{
			return true;
		}
	}
	return false;
}

void FChannel::DescribeOrder(TArray<FString>& Out) const
{
	for (const auto* Phase : {&Pre, &On, &Post, &Events})
	{
		if (!*Phase)
		{
			continue;
		}
		for (const auto& Handler : **Phase)
		{
			Out.Add(FString::Printf(TEXT("%d:%s:%s:%d"), static_cast<int32>(Handler->Stage), *Handler->ModId,
			                        *Handler->Key.ToString().ToLower(), static_cast<int32>(Handler->Operations)));
		}
	}
}

template <class T>
THookResult<T> FChannel::Invoke(UObject& Source, T Value, TFunctionRef<TCoreResult<T>(T)> Core,
                                TFunctionRef<bool(T)> ValidateInput, TFunctionRef<bool(T)> ValidateResult)
{
	const FObjectKey Key(&Source);
	if (Invoking.Contains(Key) || Invoking.Num() >= 64)
	{
		return {T{}, EError::ReentrantCall};
	}
	Invoking.Add(Key);
	ON_SCOPE_EXIT
	{
		Invoking.Remove(Key);
	};
	if (Pre)
	{
		for (const auto& Handler : *Pre)
		{
			if (!IsLive(*Handler))
			{
				continue;
			}
			const auto Decision = CallHook(*Handler, Value);
			if (Decision.Error != EError::None)
			{
				return {T{}, Decision.Error};
			}
			if (Decision.bCancel)
			{
				return EnumHasAnyFlags(Handler->Operations, EOperation::Cancel) && Decision.Value &&
				               ValidateResult(*Decision.Value)
				           ? THookResult<T>{*Decision.Value, EError::None, true}
				           : THookResult<T>{T{}, EError::InvalidArgument};
			}
			if (Decision.Value)
			{
				if (!EnumHasAnyFlags(Handler->Operations, EOperation::ModifyInputs) || !ValidateInput(*Decision.Value))
				{
					return {T{}, EError::InvalidArgument};
				}
				Value = *Decision.Value;
			}
		}
	}
	if (!IsValid(&Source))
	{
		return {T{}, EError::ObjectUnavailable};
	}
	TCoreResult<T> Original;
	if (!On || On->IsEmpty())
	{
		Original = Core(Value);
	}
	else
	{
		const FHandler* Replacement = nullptr;
		for (const auto& Handler : *On)
		{
			if (Handler->Operations == EOperation::Replace && IsLive(*Handler))
			{
				Replacement = Handler.Get();
				break;
			}
		}
		struct FContinuation
		{
			TFunction<TCoreResult<T>(T)> Call;
			TCoreResult<T> Result;
			bool bUsed = false;
			bool bRepeated = false;
		};
		const auto Run = [&](auto& Recurse, int32 Index, T Input) -> TCoreResult<T>
		{
			if (!IsValid(&Source))
			{
				return {T{}, EError::ObjectUnavailable};
			}
			while (Index < On->Num())
			{
				const auto& Handler = (*On)[Index++];
				if (Handler->Operations != EOperation::Wrap || !IsLive(*Handler))
				{
					continue;
				}
				const auto Frame = MakeShared<FContinuation>();
				Frame->Call = [&](T NextValue) -> TCoreResult<T>
				{
					return ValidateInput(NextValue) ? Recurse(Recurse, Index, NextValue)
					                                : TCoreResult<T>{T{}, EError::InvalidArgument};
				};
				const TNext<T> Next = [Frame](T NextValue) -> TCoreResult<T>
				{
					if (!IsInGameThread())
					{
						return {T{}, EError::WrongThread};
					}
					if (!Frame->Call)
					{
						return {T{}, EError::BindingExpired};
					}
					if (Frame->bUsed)
					{
						Frame->bRepeated = true;
						return {T{}, EError::ReentrantCall};
					}
					Frame->bUsed = true;
					Frame->Result = Frame->Call(NextValue);
					return Frame->Result;
				};
				ON_SCOPE_EXIT
				{
					Frame->Call = {};
				};
				TSharedPtr<const uint8> Lifetime;
				if constexpr (std::is_same_v<T, FPayload>)
				{
					if (Handler->Project)
					{
						Lifetime = MakeShared<uint8>(0);
						auto Projected = Handler->Project(Input, Lifetime.ToSharedRef());
						if (!Projected.IsSuccess())
						{
							return {T{}, Projected.GetError()};
						}
						Input = MoveTemp(Projected.GetValue());
					}
				}
				auto Result = Handler->GetTypedCalls<T>()->Wrap(Input, Next);
				Result.bCommitted |= Frame->Result.bCommitted;
				if (Result.Error == EError::None)
				{
					Result.Error = Frame->bRepeated ? EError::ReentrantCall
					               : !Frame->bUsed  ? EError::InvalidArgument
					                                : Frame->Result.Error;
				}
				return Result;
			}
			if (Replacement && IsLive(*Replacement))
			{
				TSharedPtr<const uint8> Lifetime;
				if constexpr (std::is_same_v<T, FPayload>)
				{
					if (Replacement->Project)
					{
						Lifetime = MakeShared<uint8>(0);
						auto Projected = Replacement->Project(Input, Lifetime.ToSharedRef());
						if (!Projected.IsSuccess())
						{
							return {T{}, Projected.GetError()};
						}
						Input = MoveTemp(Projected.GetValue());
					}
				}
				return Replacement->GetTypedCalls<T>()->Replace(Input);
			}
			return Core(Input);
		};
		Original = Run(Run, 0, Value);
		if (Original.Error == EError::None && !ValidateResult(Original.Value))
		{
			Original.Error = EError::InvalidArgument;
		}
	}
	THookResult<T> Result{Original.Value, Original.Error, false, Original.bCommitted};
	if (Result.Error == EError::None && Post)
	{
		for (const auto& Handler : *Post)
		{
			if (!IsLive(*Handler))
			{
				continue;
			}
			if (!IsValid(&Source))
			{
				Result.Error = EError::ObjectUnavailable;
				break;
			}
			const auto Decision = CallHook(*Handler, Result.Value);
			Result.Error = Decision.Error;
			if (Result.Error != EError::None)
			{
				break;
			}
			if (Decision.bCancel ||
			    (Decision.Value &&
			     (!EnumHasAnyFlags(Handler->Operations, EOperation::ModifyResult) || !ValidateResult(*Decision.Value))))
			{
				Result.Error = EError::InvalidArgument;
				break;
			}
			if (Decision.Value)
			{
				Result.Value = *Decision.Value;
			}
		}
	}
	return Result;
}

template <class T> API::EError FChannel::Emit(UObject& Source, T Value)
{
	if (bPublishing)
	{
		return EError::ReentrantCall;
	}
	TGuardValue<bool> Guard(bPublishing, true);
	if (Events)
	{
		for (const auto& Handler : *Events)
		{
			if (!IsValid(&Source))
			{
				return EError::ObjectUnavailable;
			}
			if ((!Definition.bImmediateUnsubscribe || !ImmediatelyRemoved.Contains(Handler->Serial)) &&
			    IsLive(*Handler))
			{
				const EError Error = CallEvent(*Handler, Value);
				if (Error != EError::None)
				{
					UE_LOG(LogModExtension, Warning, TEXT("Event %s listener %s/%s failed (%d)."),
					       *Definition.Id.ToString(), *Handler->ModId, *Handler->Key.ToString(),
					       static_cast<int32>(Error));
				}
			}
		}
	}
	return EError::None;
}

#define UMF_INSTANTIATE_CHANNEL(Scalar)                                                                                \
	template THookResult<Scalar> FChannel::Invoke(UObject&, Scalar, TFunctionRef<TCoreResult<Scalar>(Scalar)>,         \
	                                              TFunctionRef<bool(Scalar)>, TFunctionRef<bool(Scalar)>);             \
	template EError FChannel::Emit(UObject&, Scalar)

UMF_INSTANTIATE_CHANNEL(float);
UMF_INSTANTIATE_CHANNEL(double);
UMF_INSTANTIATE_CHANNEL(int32);
UMF_INSTANTIATE_CHANNEL(int64);
UMF_INSTANTIATE_CHANNEL(bool);
UMF_INSTANTIATE_CHANNEL(uint8);
UMF_INSTANTIATE_CHANNEL(FPayload);
#undef UMF_INSTANTIATE_CHANNEL

FGeneratedRegistration* FGeneratedRegistration::First = nullptr;

FGeneratedRegistration::FGeneratedRegistration(FDescribe InDescribe) : Next(First), Describe(InDescribe)
{
	First = this;
}

FGeneratedRegistration::~FGeneratedRegistration()
{
	for (FGeneratedRegistration** Link = &First; *Link; Link = &(*Link)->Next)
	{
		if (*Link == this)
		{
			*Link = Next;
			break;
		}
	}
}

void FGeneratedRegistration::DescribeAll(TArray<FDefinition>& Out)
{
	check(IsInGameThread());
	for (const FGeneratedRegistration* Entry = First; Entry; Entry = Entry->Next)
	{
		Entry->Describe(Out);
	}
}
} // namespace Mod::Extension
