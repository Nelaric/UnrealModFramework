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

FHookResult RunCore(float Value, TFunctionRef<FCoreResult(float)> Core)
{
	const FCoreResult Result = Core(Value);
	return {Result.Value, Result.Error, false, Result.bCommitted};
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
	Desired.RemoveAll([&Owner, Serial](const auto& Handler)
	                  { return Handler->Serial == Serial && Handler->Owner.Get() == &Owner; });
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
	return SortStage(EStage::Pre, PendingPre) && SortStage(EStage::Post, PendingPost) &&
	       SortStage(EStage::Event, PendingEvents);
}

void FChannel::Publish()
{
	Pre = MoveTemp(PendingPre);
	Post = MoveTemp(PendingPost);
	Events = MoveTemp(PendingEvents);
}

void FChannel::Discard()
{
	PendingPre.Reset();
	PendingPost.Reset();
	PendingEvents.Reset();
	Desired.Reset();
	for (const auto* Snapshot : {&Pre, &Post, &Events})
	{
		if (*Snapshot)
		{
			Desired.Append(**Snapshot);
		}
	}
}

FHookResult FChannel::Invoke(UObject& Source, float Value, TFunctionRef<FCoreResult(float)> Core,
                             TFunctionRef<bool(float)> ValidateInput, TFunctionRef<bool(float)> ValidateResult)
{
	const FObjectKey Key(&Source);
	if (Invoking.Contains(Key) || Invoking.Num() >= 64)
	{
		return {0, EError::ReentrantCall};
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
			const FHookDecision Decision = Handler->Hook(Value);
			if (Decision.Error != EError::None)
			{
				return {0, Decision.Error};
			}
			if (Decision.bCancel)
			{
				return EnumHasAnyFlags(Handler->Operations, EOperation::Cancel) && Decision.Value &&
				               ValidateResult(*Decision.Value)
				           ? FHookResult{*Decision.Value, EError::None, true}
				           : FHookResult{0, EError::InvalidArgument};
			}
			if (Decision.Value)
			{
				if (!EnumHasAnyFlags(Handler->Operations, EOperation::ModifyInputs) || !ValidateInput(*Decision.Value))
				{
					return {0, EError::InvalidArgument};
				}
				Value = *Decision.Value;
			}
		}
	}
	if (!IsValid(&Source))
	{
		return {0, EError::ObjectUnavailable};
	}
	FHookResult Result = RunCore(Value, Core);
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
			const FHookDecision Decision = Handler->Hook(Result.Value);
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

API::EError FChannel::Emit(UObject& Source, float Value)
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
			if (IsLive(*Handler))
			{
				const EError Error = Handler->Event(Value);
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
