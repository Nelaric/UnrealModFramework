// Copyright (c) 2026 Nelaric Contributors
#include "ModExtensionWorld.h"

#include "ModInstance.h"
#include "UObject/UnrealType.h"

using namespace Mod::Extension;
using Mod::API::EBlueprintParameter;
using Mod::API::EError;
using Mod::API::FBlueprintCall;
using Mod::API::FBlueprintParameter;

FHookResult UModExtensionSubsystem::InvokeBlueprint(UObject& Source, FName Id, float Value)
{
	if (!IsReady() || Source.GetWorld() != GetWorld())
	{
		return {0, EError::NotReady};
	}
	if (!FMath::IsFinite(Value))
	{
		return {0, EError::InvalidArgument};
	}
	const auto* Entry = State->Channels.Find(Id);
	if (!Entry || (*Entry)->GetDefinition().BlueprintCore.IsNone())
	{
		return {0, EError::NotRegistered};
	}
	const auto& Definition = (*Entry)->GetDefinition();
	const TPair<FName, TWeakObjectPtr<UClass>> Key(Id, Source.GetClass());
	auto Call = State->BlueprintCalls.FindRef(Key);
	if (!Call || !Call->IsCurrent())
	{
		UClass* Declaring = Definition.BlueprintClass.ResolveClass();
		if (!Declaring || !Source.IsA(Declaring))
		{
			return {0, EError::BindingExpired};
		}
		const FBlueprintParameter Parameters[]{
		    {Definition.BlueprintParameter, TEXT("FloatProperty"), EBlueprintParameter::Input},
		    {Definition.BlueprintResult, TEXT("FloatProperty"), Definition.BlueprintResultRole}};
		const auto Binding =
		    FBlueprintCall::Bind(*Declaring, *Source.GetClass(), {Definition.BlueprintCore, Parameters});
		if (!Binding.IsSuccess())
		{
			return {0, Binding.GetError()};
		}
		Call = Binding.GetValue();
		State->BlueprintCalls.Add(Key, Call);
	}
	FEndpoint Endpoint{GetWorld(), *Entry, State, Id};
	return Invoke(
	    Source, Id, Endpoint, Value,
	    [&Call, &Source](float Input)
	    {
		    float Output = 0;
		    const auto Error = Call->Process(
		        Source,
		        [&](void* Buffer) {
			        static_cast<const FFloatProperty*>(Call->GetParameter(0))
			            ->SetPropertyValue_InContainer(Buffer, Input);
		        },
		        [&](const void* Buffer) {
			        Output =
			            static_cast<const FFloatProperty*>(Call->GetParameter(1))->GetPropertyValue_InContainer(Buffer);
		        });
		    const EError Status = Error == EError::None && !FMath::IsFinite(Output) ? EError::InvalidArgument : Error;
		    return FCoreResult{Output, Status, true};
	    },
	    [](float Input) { return FMath::IsFinite(Input); }, [](float Result) { return FMath::IsFinite(Result); });
}

EError UModExtensionSubsystem::PublishBlueprint(UObject& Source, FName Id, float Value)
{
	if (!IsReady() || Source.GetWorld() != GetWorld())
	{
		return EError::NotReady;
	}
	const auto* Entry = State->Channels.Find(Id);
	UClass* Declaring = Entry ? (*Entry)->GetDefinition().BlueprintClass.ResolveClass() : nullptr;
	if (!Declaring || !Source.IsA(Declaring))
	{
		return EError::PermissionDenied;
	}
	FEndpoint Endpoint{GetWorld(), *Entry, State, Id};
	return Emit(Source, Id, Endpoint, Value);
}

TModResult<FSubscription> UModExtensionSubsystem::RegisterBlueprint(UModInstance& Owner, FName Id, FName Key,
                                                                    FName Function, EStage Stage, EOperation Operation,
                                                                    FOrder Order, float CancelValue)
{
	using FResult = TModResult<FSubscription>;
	if (!IsInGameThread())
	{
		return FResult::Failure(EError::WrongThread);
	}
	if (!State || !State->Owns(Owner))
	{
		return FResult::Failure(EError::NotReady);
	}
	const auto Fail = [&](EError Error)
	{
		if (State->Phase == FModExtensionWorld::EState::Preparing && State->RegistrationError.IsEmpty())
		{
			State->RegistrationError = Owner.GetModId() + TEXT(": invalid extension callback ") + Function.ToString();
		}
		return FResult::Failure(Error);
	};
	if (Operation != EOperation::None && Operation != EOperation::ModifyInputs && Operation != EOperation::Cancel &&
	    Operation != EOperation::ModifyResult)
	{
		return Fail(EError::InvalidArgument);
	}
	UClass* Class = Owner.GetClass();
	UFunction* FunctionObject = Class->FindFunctionByName(Function);
	if (!FunctionObject)
	{
		return Fail(EError::NotRegistered);
	}
	const auto Shape = FBlueprintCall::Describe(*FunctionObject);
	if (!Shape.IsSuccess())
	{
		return Fail(Shape.GetError());
	}
	const bool bObserve = Operation == EOperation::None;
	const auto& Parameters = Shape.GetValue();
	const FName OutputType(Operation == EOperation::Cancel ? TEXT("BoolProperty") : TEXT("FloatProperty"));
	if (Parameters.Num() != (bObserve ? 1 : 2) || Parameters[0].Role != EBlueprintParameter::Input ||
	    Parameters[0].PropertyClass != TEXT("FloatProperty") ||
	    (!bObserve && (Parameters[1].Role == EBlueprintParameter::Input || Parameters[1].PropertyClass != OutputType)))
	{
		return Fail(EError::SignatureMismatch);
	}
	const auto Bound = FBlueprintCall::Bind(*FunctionObject->GetOuterUClass(), *Class, {Function, Parameters});
	if (!Bound.IsSuccess())
	{
		return Fail(Bound.GetError());
	}
	const auto Call = Bound.GetValue();
	const TWeakObjectPtr<UModInstance> Target(&Owner);
	const auto Process = [Target, Call](float Value, TFunctionRef<void(const void*)> Read)
	{
		if (UModInstance* Instance = Target.Get())
		{
			return Call->Process(
			    *Instance,
			    [&](void* Buffer) {
				    static_cast<const FFloatProperty*>(Call->GetParameter(0))
				        ->SetPropertyValue_InContainer(Buffer, Value);
			    },
			    Read);
		}
		return EError::BindingExpired;
	};
	FHandler Handler;
	Handler.Target = &Owner;
	Handler.Stage = Stage;
	Handler.Operations = Operation;
	Handler.Function = Function;
	Handler.Key = Key;
	Handler.Order = MoveTemp(Order);
	Handler.CancelValue = CancelValue;
	if (Stage == EStage::Event)
	{
		Handler.Event = [Process](float Value) { return Process(Value, [](const void*) {}); };
	}
	else
	{
		Handler.Hook = [Process, Call, Operation, CancelValue](float Value)
		{
			FHookDecision Decision;
			Decision.Error = Process(Value,
			                         [&](const void* Buffer)
			                         {
				                         if (Operation == EOperation::Cancel)
				                         {
					                         Decision.bCancel = static_cast<const FBoolProperty*>(Call->GetParameter(1))
					                                                ->GetPropertyValue_InContainer(Buffer);
					                         if (Decision.bCancel)
					                         {
						                         Decision.Value = CancelValue;
					                         }
				                         }
				                         else if (Operation != EOperation::None)
				                         {
					                         Decision.Value = static_cast<const FFloatProperty*>(Call->GetParameter(1))
					                                              ->GetPropertyValue_InContainer(Buffer);
				                         }
			                         });
			return Decision;
		};
	}
	return Register(Owner, Id, FString(), MoveTemp(Handler));
}
