// Copyright (c) 2026 Nelaric Contributors
#include "ModExtensionWorld.h"

#include <limits>

#include "ModInstance.h"
#include "ModExtensionLibrary.h"
#include "ModAPIHostSubsystem.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

using namespace Mod::Extension;
using Mod::API::EBlueprintParameter;
using Mod::API::EError;
using Mod::API::FBlueprintCall;
using Mod::API::FBlueprintParameter;

namespace
{
template <class T> struct TScalarProperty;
template <> struct TScalarProperty<float>
{
	using FType = FFloatProperty;
	static constexpr const TCHAR* Name = TEXT("FloatProperty");
};
template <> struct TScalarProperty<double>
{
	using FType = FDoubleProperty;
	static constexpr const TCHAR* Name = TEXT("DoubleProperty");
};
template <> struct TScalarProperty<int32>
{
	using FType = FIntProperty;
	static constexpr const TCHAR* Name = TEXT("IntProperty");
};
template <> struct TScalarProperty<int64>
{
	using FType = FInt64Property;
	static constexpr const TCHAR* Name = TEXT("Int64Property");
};
template <> struct TScalarProperty<bool>
{
	using FType = FBoolProperty;
	static constexpr const TCHAR* Name = TEXT("BoolProperty");
};
template <> struct TScalarProperty<uint8>
{
	using FType = FByteProperty;
	static constexpr const TCHAR* Name = TEXT("ByteProperty");
};

template <class T> bool IsScalarValid(T Value)
{
	if constexpr (std::is_floating_point_v<T>)
	{
		return FMath::IsFinite(Value);
	}
	else
	{
		return true;
	}
}
void CopyProjectedFields(const UScriptStruct& SourceType, const void* Source, const UScriptStruct& DestinationType,
                         void* Destination)
{
	TFieldIterator<FProperty> From(&SourceType, EFieldIteratorFlags::ExcludeSuper);
	TFieldIterator<FProperty> To(&DestinationType, EFieldIteratorFlags::ExcludeSuper);
	for (; From && To; ++From, ++To)
	{
		const void* Input = From->ContainerPtrToValuePtr<void>(Source);
		void* Output = To->ContainerPtrToValuePtr<void>(Destination);
		if (const auto* Nested = CastField<FStructProperty>(*From))
		{
			CopyProjectedFields(*Nested->Struct, Input, *CastFieldChecked<FStructProperty>(*To)->Struct, Output);
		}
		else if (const auto* Boolean = CastField<FBoolProperty>(*From))
		{
			CastFieldChecked<FBoolProperty>(*To)->SetPropertyValue(Output, Boolean->GetPropertyValue(Input));
		}
		else
		{
			To->CopySingleValue(Output, Input);
		}
	}
}
} // namespace

FHookResult UModExtensionSubsystem::InvokeBlueprint(UObject& Source, FName Id, float Value)
{
	return InvokeBlueprintScalar<float>(Source, Id, Value);
}

template <class T> THookResult<T> UModExtensionSubsystem::InvokeBlueprintScalar(UObject& Source, FName Id, T Value)
{
	using FPropertyType = typename TScalarProperty<T>::FType;
	const FName PropertyClass(TScalarProperty<T>::Name);
	if (!IsInGameThread())
	{
		return {0, EError::WrongThread};
	}
	if (!State || Source.GetWorld() != GetWorld())
	{
		return {0, EError::NotReady};
	}
	if (!IsScalarValid(Value))
	{
		return {0, EError::InvalidArgument};
	}
	if (State->Phase == FModExtensionWorld::EState::New)
	{
		TArray<FDefinition> Definitions;
		FGeneratedRegistration::DescribeAll(Definitions);
		const auto* Found = Definitions.FindByPredicate([Id](const auto& Definition) { return Definition.Id == Id; });
		if (!Found || Found->BlueprintCore.IsNone() || Found->Scalar != ScalarType<T>())
		{
			return {0, EError::NotRegistered};
		}
		State->Channels.FindOrAdd(Id, MakeShared<FChannel>(*Found));
	}
	const auto* Entry = State->Channels.Find(Id);
	if (!Entry || (*Entry)->GetDefinition().BlueprintCore.IsNone())
	{
		return {0, EError::NotRegistered};
	}
	const auto& Definition = (*Entry)->GetDefinition();
	if (Definition.Scalar != ScalarType<T>())
	{
		return {T{}, EError::SignatureMismatch};
	}
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
		    {Definition.BlueprintParameter, PropertyClass, EBlueprintParameter::Input},
		    {Definition.BlueprintResult, PropertyClass, Definition.BlueprintResultRole}};
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
	return InvokeScalar<T>(
	    Source, Id, Endpoint, Value,
	    [&Call, &Source](T Input)
	    {
		    T Output{};
		    const auto Error = Call->Process(
		        Source,
		        [&](void* Buffer) {
			        static_cast<const FPropertyType*>(Call->GetParameter(0))
			            ->SetPropertyValue_InContainer(Buffer, Input);
		        },
		        [&](const void* Buffer) {
			        Output =
			            static_cast<const FPropertyType*>(Call->GetParameter(1))->GetPropertyValue_InContainer(Buffer);
		        });
		    const EError Status = Error == EError::None && !IsScalarValid(Output) ? EError::InvalidArgument : Error;
		    return TCoreResult<T>{Output, Status, Error == EError::None};
	    },
	    &IsScalarValid<T>, &IsScalarValid<T>);
}

EError UModExtensionSubsystem::PublishBlueprint(UObject& Source, FName Id, float Value)
{
	return PublishBlueprintScalar<float>(Source, Id, Value);
}

THookResult<FPayload> UModExtensionSubsystem::InvokeBlueprintPayload(UObject& Source, FName Id, FPayload Value)
{
	if (!IsInGameThread() || !State || Source.GetWorld() != GetWorld())
	{
		return {{}, EError::NotReady};
	}
	if (State->Phase == FModExtensionWorld::EState::New && !State->Channels.Contains(Id))
	{
		TArray<FDefinition> Definitions;
		FGeneratedRegistration::DescribeAll(Definitions);
		if (const auto* Declared = Definitions.FindByPredicate([Id](const auto& Item) { return Item.Id == Id; }))
		{
			State->Channels.Add(Id, MakeShared<FChannel>(*Declared));
		}
	}
	const auto Channel = State->Channels.FindRef(Id);
	if (!Channel || Channel->GetDefinition().BlueprintCore.IsNone())
	{
		return {{}, EError::NotRegistered};
	}
	const auto& Definition = Channel->GetDefinition();
	UScriptStruct* Type = Cast<UScriptStruct>(Definition.Payload.Struct.ResolveObject());
	UClass* ObjectType = Definition.Payload.Object.ResolveClass();
	UClass* Declaring = Definition.BlueprintClass.ResolveClass();
	if ((!Type && !ObjectType) || !Declaring || !Source.IsA(Declaring) ||
	    !Definition.Payload.Matches(Value, GetWorld()) ||
	    (Type && Definition.Payload.Layout != DescribePayloadStruct(*Type)))
	{
		return {{}, EError::SignatureMismatch};
	}
	const TPair<FName, TWeakObjectPtr<UClass>> Key(Id, Source.GetClass());
	auto Call = State->BlueprintCalls.FindRef(Key);
	if (!Call || !Call->IsCurrent())
	{
		const FName TypePath(Type ? Type->GetPathName() : ObjectType->GetPathName());
		const FName PropertyClass(Type ? TEXT("StructProperty") : TEXT("ObjectProperty"));
		const FBlueprintParameter Parameters[]{
		    {Definition.BlueprintParameter, PropertyClass, EBlueprintParameter::Input, TypePath},
		    {Definition.BlueprintResult, PropertyClass, Definition.BlueprintResultRole, TypePath}};
		auto Bound = FBlueprintCall::Bind(*Declaring, *Source.GetClass(), {Definition.BlueprintCore, Parameters}, true);
		if (!Bound.IsSuccess())
		{
			return {{}, Bound.GetError()};
		}
		Call = Bound.GetValue();
		State->BlueprintCalls.Add(Key, Call);
	}
	FEndpoint Endpoint{GetWorld(), Channel, State, Id};
	const auto Validate = [&](const FPayload& Data) { return Definition.Payload.Matches(Data, GetWorld()); };
	return InvokeScalar<FPayload>(
	    Source, Id, Endpoint, MoveTemp(Value),
	    [&Source, Type, Call](const FPayload& Input)
	    {
		    FPayload Output;
		    const EError Status = Call->Process(
		        Source,
		        [&](void* Buffer)
		        {
			        auto* Property = Call->GetParameter(0);
			        if (Type)
			        {
				        Type->CopyScriptStruct(Property->ContainerPtrToValuePtr<void>(Buffer),
				                               Input.GetStruct().GetMemory());
			        }
			        else
			        {
				        CastFieldChecked<FObjectProperty>(Property)->SetObjectPropertyValue_InContainer(
				            Buffer, const_cast<UObject*>(Input.GetObject()));
			        }
		        },
		        [&](const void* Buffer)
		        {
			        auto* Property = Call->GetParameter(1);
			        Output = Type ? FPayload::FromStruct(*Type, Property->ContainerPtrToValuePtr<void>(Buffer))
			                      : FPayload::FromObject(
			                            CastFieldChecked<FObjectProperty>(Property)->GetObjectPropertyValue_InContainer(
			                                Buffer));
		        });
		    return TCoreResult<FPayload>{MoveTemp(Output), Status, Status == EError::None};
	    },
	    Validate, Validate);
}

template <class T> EError UModExtensionSubsystem::PublishBlueprintScalar(UObject& Source, FName Id, T Value)
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
	return EmitScalar<T>(Source, Id, Endpoint, Value);
}

template THookResult<float> UModExtensionSubsystem::InvokeBlueprintScalar(UObject&, FName, float);
template EError UModExtensionSubsystem::PublishBlueprintScalar(UObject&, FName, float);
template THookResult<double> UModExtensionSubsystem::InvokeBlueprintScalar(UObject&, FName, double);
template EError UModExtensionSubsystem::PublishBlueprintScalar(UObject&, FName, double);
template THookResult<int32> UModExtensionSubsystem::InvokeBlueprintScalar(UObject&, FName, int32);
template EError UModExtensionSubsystem::PublishBlueprintScalar(UObject&, FName, int32);
template THookResult<int64> UModExtensionSubsystem::InvokeBlueprintScalar(UObject&, FName, int64);
template EError UModExtensionSubsystem::PublishBlueprintScalar(UObject&, FName, int64);
template THookResult<bool> UModExtensionSubsystem::InvokeBlueprintScalar(UObject&, FName, bool);
template EError UModExtensionSubsystem::PublishBlueprintScalar(UObject&, FName, bool);
template THookResult<uint8> UModExtensionSubsystem::InvokeBlueprintScalar(UObject&, FName, uint8);
template EError UModExtensionSubsystem::PublishBlueprintScalar(UObject&, FName, uint8);
template EError UModExtensionSubsystem::PublishBlueprintScalar(UObject&, FName, FPayload);

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
	    Operation != EOperation::ModifyResult && Operation != EOperation::Replace)
	{
		return Fail(EError::InvalidArgument);
	}
	UClass* Class = Owner.GetClass();
	UFunction* FunctionObject = Class->FindFunctionByName(Function);
	if (!FunctionObject)
	{
		return Fail(EError::NotRegistered);
	}
	const auto Channel = State->Channels.FindRef(Id);
	if (!Channel)
	{
		return Fail(EError::NotRegistered);
	}
	const auto& Definition = Channel->GetDefinition();
	if (Definition.Payload.IsObject())
	{
		if (Operation != EOperation::None || Stage == EStage::On)
		{
			return Fail(EError::InvalidArgument);
		}
		const auto Shape = FBlueprintCall::Describe(*FunctionObject, true);
		if (!Shape.IsSuccess() || Shape.GetValue().Num() != 1 ||
		    Shape.GetValue()[0].TypePath != FName(FModPayloadObject::StaticStruct()->GetPathName()) ||
		    Shape.GetValue()[0].Role != EBlueprintParameter::Input)
		{
			return Fail(EError::SignatureMismatch);
		}
		const auto Bound =
		    FBlueprintCall::Bind(*FunctionObject->GetOuterUClass(), *Class, {Function, Shape.GetValue()}, true);
		if (!Bound.IsSuccess())
		{
			return Fail(Bound.GetError());
		}
		const auto Process = [WeakState = TWeakPtr<FModExtensionWorld>(State),
		                      WeakOwner = TWeakObjectPtr<UModInstance>(&Owner), Call = Bound.GetValue(),
		                      ViewId = Definition.Payload.TypeId](const FPayload& Value)
		{
			auto* Instance = WeakOwner.Get();
			const auto World = WeakState.Pin();
			const auto* Caller = World && Instance ? World->Callers.Find(FObjectKey(Instance)) : nullptr;
			if (!Caller || !World->APIHostOwner.IsValid() || World->APIHostOwner->GetRegistry() != World->APIRegistry)
			{
				return EError::BindingExpired;
			}
			FModPayloadObject Projected;
			Projected.ViewId = ViewId;
			ON_SCOPE_EXIT
			{
				if (Projected.Context)
				{
					Mod::API::FNativeAccess::Close(Projected.Context);
				}
			};
			if (!Value.IsNullObject())
			{
				auto Binding = Value.GetObjectView();
				if (!Binding.IsSuccess())
				{
					return Binding.GetError();
				}
				const auto Context = Mod::API::FNativeAccess::Open(*World->APIRegistry, *Caller, false);
				if (!Context.IsSuccess())
				{
					return Context.GetError();
				}
				Projected.Context = Context.GetValue().Context;
				auto Token = Mod::API::FNativeAccess::Publish(Projected.Context, MoveTemp(Binding.GetValue()));
				if (!Token.IsSuccess())
				{
					return Token.GetError();
				}
				Projected.Object = Token.GetValue();
			}
			return Call->Process(
			    *Instance, [&](void* Buffer)
			    { *Call->GetParameter(0)->ContainerPtrToValuePtr<FModPayloadObject>(Buffer) = Projected; },
			    [](const void*) {});
		};
		THandlerCalls<FPayload> Calls;
		if (Stage == EStage::Event)
		{
			Calls.Event = Process;
		}
		else
		{
			Calls.Hook = [Process](FPayload Value) { return THookDecision<FPayload>{{}, Process(Value)}; };
		}
		FHandler Handler;
		Handler.Target = &Owner;
		Handler.Stage = Stage;
		Handler.Function = Function;
		Handler.Key = Key;
		Handler.Order = MoveTemp(Order);
		Handler.SetCalls<FPayload>(MoveTemp(Calls));
		return Register(Owner, Id, FString(), MoveTemp(Handler));
	}
	if (Definition.Payload.IsStruct())
	{
		auto* Type = Cast<UScriptStruct>(Definition.Payload.Struct.ResolveObject());
		if (!Type || Definition.Payload.Layout != DescribePayloadStruct(*Type))
		{
			return Fail(EError::SignatureMismatch);
		}
		const auto Shape = FBlueprintCall::Describe(*FunctionObject, true);
		const bool bObserve = Operation == EOperation::None;
		if (!Shape.IsSuccess() || Shape.GetValue().Num() != (bObserve ? 1 : 2) ||
		    Shape.GetValue()[0].Role != EBlueprintParameter::Input ||
		    (!bObserve && Shape.GetValue()[1].Role == EBlueprintParameter::Input))
		{
			return Fail(EError::SignatureMismatch);
		}
		const auto Bound =
		    FBlueprintCall::Bind(*FunctionObject->GetOuterUClass(), *Class, {Function, Shape.GetValue()}, true);
		if (!Bound.IsSuccess())
		{
			return Fail(Bound.GetError());
		}
		const auto& Call = Bound.GetValue();
		const auto* Input = CastField<FStructProperty>(Call->GetParameter(0));
		const auto* Output = bObserve ? nullptr : CastField<FStructProperty>(Call->GetParameter(1));
		if (!Input || (Input->Struct != Type && !Definition.Payload.MatchesBlueprint(*Input->Struct)) ||
		    (!bObserve && (!Output || Output->Struct != Input->Struct)))
		{
			return Fail(EError::SignatureMismatch);
		}
		const auto Process = [Target = TWeakObjectPtr<UModInstance>(&Owner), Call, Type, Input,
		                      Output](const FPayload& Value, FPayload& Result)
		{
			auto* Instance = Target.Get();
			if (!Instance)
			{
				return EError::BindingExpired;
			}
			return Call->Process(
			    *Instance,
			    [&](void* Buffer)
			    {
				    void* Destination = Input->ContainerPtrToValuePtr<void>(Buffer);
				    const void* Source = Value.GetStruct().GetMemory();
				    if (Input->Struct == Type)
				    {
					    Type->CopyScriptStruct(Destination, Source);
				    }
				    else
				    {
					    CopyProjectedFields(*Type, Source, *Input->Struct, Destination);
				    }
			    },
			    [&](const void* Buffer)
			    {
				    if (!Output)
				    {
					    return;
				    }
				    const void* Source = Output->ContainerPtrToValuePtr<void>(Buffer);
				    if (Output->Struct == Type)
				    {
					    Result = FPayload::FromStruct(*Type, Source);
				    }
				    else
				    {
					    FInstancedStruct Destination(Type);
					    CopyProjectedFields(*Output->Struct, Source, *Type, Destination.GetMutableMemory());
					    Result = FPayload::FromStruct(*Type, Destination.GetMemory());
				    }
			    });
		};
		THandlerCalls<FPayload> Calls;
		if (Stage == EStage::Event && bObserve)
		{
			Calls.Event = [Process](FPayload Value)
			{
				FPayload Ignored;
				return Process(Value, Ignored);
			};
		}
		else if (Operation == EOperation::Replace)
		{
			Calls.Replace = [Process](FPayload Value)
			{
				FPayload Result;
				const auto Error = Process(Value, Result);
				return TCoreResult<FPayload>{MoveTemp(Result), Error, Error == EError::None};
			};
		}
		else
		{
			Calls.Hook = [Process, bObserve, Operation](FPayload Value)
			{
				FPayload Result;
				const auto Error = Process(Value, Result);
				THookDecision<FPayload> Decision;
				Decision.Error = Error;
				if (!bObserve && Error == EError::None)
				{
					Decision.bCancel = Operation == EOperation::Cancel;
					Decision.Value = MoveTemp(Result);
				}
				return Decision;
			};
		}
		FHandler Handler;
		Handler.Target = &Owner;
		Handler.Stage = Stage;
		Handler.Operations = Operation;
		Handler.Function = Function;
		Handler.Key = Key;
		Handler.Order = MoveTemp(Order);
		Handler.SetCalls<FPayload>(MoveTemp(Calls));
		return Register(Owner, Id, FString(), MoveTemp(Handler));
	}
	const auto Shape = FBlueprintCall::Describe(*FunctionObject);
	if (!Shape.IsSuccess())
	{
		return Fail(Shape.GetError());
	}
	const auto& Parameters = Shape.GetValue();
	const auto RegisterTyped = [&]<class T>() -> FResult
	{
		const FName PropertyClass(TScalarProperty<T>::Name);
		const bool bObserve = Operation == EOperation::None;
		const FName OutputType = Operation == EOperation::Cancel ? FName(TEXT("BoolProperty")) : PropertyClass;
		if (Parameters.Num() != (bObserve ? 1 : 2) || Parameters[0].Role != EBlueprintParameter::Input ||
		    Parameters[0].PropertyClass != PropertyClass ||
		    (!bObserve &&
		     (Parameters[1].Role == EBlueprintParameter::Input || Parameters[1].PropertyClass != OutputType)))
		{
			return Fail(EError::SignatureMismatch);
		}
		T Cancel{};
		if (Operation == EOperation::Cancel)
		{
			if constexpr (std::is_integral_v<T>)
			{
				// The legacy Blueprint registration node supplies a Float.
				const double Number = CancelValue;
				const double Min = static_cast<double>((std::numeric_limits<T>::lowest)());
				const double Max = static_cast<double>((std::numeric_limits<T>::max)());
				const bool bPastMax = std::is_same_v<T, int64> ? Number >= Max : Number > Max;
				if (!FMath::IsFinite(Number) || Number < Min || bPastMax || Number != FMath::TruncToDouble(Number))
				{
					return Fail(EError::InvalidArgument);
				}
			}
			Cancel = static_cast<T>(CancelValue);
		}
		const auto Bound = FBlueprintCall::Bind(*FunctionObject->GetOuterUClass(), *Class, {Function, Parameters});
		if (!Bound.IsSuccess())
		{
			return Fail(Bound.GetError());
		}
		const auto Call = Bound.GetValue();
		const TWeakObjectPtr<UModInstance> Target(&Owner);
		const auto Process = [Target, Call](T Value, TFunctionRef<void(const void*)> Read)
		{
			using FPropertyType = typename TScalarProperty<T>::FType;
			if (UModInstance* Instance = Target.Get())
			{
				return Call->Process(
				    *Instance,
				    [&](void* Buffer) {
					    static_cast<const FPropertyType*>(Call->GetParameter(0))
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
		THandlerCalls<T> Calls;
		if (Stage == EStage::Event)
		{
			Calls.Event = [Process](T Value) { return Process(Value, [](const void*) {}); };
		}
		else if (Operation == EOperation::Replace)
		{
			Calls.Replace = [Process, Call](T Value)
			{
				using FPropertyType = typename TScalarProperty<T>::FType;
				T Output{};
				const EError Error = Process(Value,
				                             [&](const void* Buffer) {
					                             Output = static_cast<const FPropertyType*>(Call->GetParameter(1))
					                                          ->GetPropertyValue_InContainer(Buffer);
				                             });
				return TCoreResult<T>{Output, Error, Error == EError::None};
			};
		}
		else
		{
			Calls.Hook = [Process, Call, Operation, Cancel](T Value)
			{
				using FPropertyType = typename TScalarProperty<T>::FType;
				THookDecision<T> Decision;
				Decision.Error =
				    Process(Value,
				            [&](const void* Buffer)
				            {
					            if (Operation == EOperation::Cancel)
					            {
						            Decision.bCancel = static_cast<const FBoolProperty*>(Call->GetParameter(1))
						                                   ->GetPropertyValue_InContainer(Buffer);
						            if (Decision.bCancel)
						            {
							            Decision.Value = Cancel;
						            }
					            }
					            else if (Operation != EOperation::None)
					            {
						            Decision.Value = static_cast<const FPropertyType*>(Call->GetParameter(1))
						                                 ->GetPropertyValue_InContainer(Buffer);
					            }
				            });
				return Decision;
			};
		}
		Handler.SetCalls<T>(MoveTemp(Calls));
		return Register(Owner, Id, FString(), MoveTemp(Handler));
	};
	switch (Channel->GetDefinition().Scalar)
	{
	case EScalar::Float:
		return RegisterTyped.template operator()<float>();
	case EScalar::Double:
		return RegisterTyped.template operator()<double>();
	case EScalar::Int32:
		return RegisterTyped.template operator()<int32>();
	case EScalar::Int64:
		return RegisterTyped.template operator()<int64>();
	case EScalar::Bool:
		return RegisterTyped.template operator()<bool>();
	case EScalar::Byte:
		return RegisterTyped.template operator()<uint8>();
	case EScalar::Projected:
		break;
	}
	return Fail(EError::SignatureMismatch);
}
