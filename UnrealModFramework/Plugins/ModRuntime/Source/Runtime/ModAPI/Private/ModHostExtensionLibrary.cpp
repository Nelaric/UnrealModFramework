// Copyright (c) 2026 Nelaric Contributors
#include "ModHostExtensionLibrary.h"

#include "Engine/World.h"
#include "ModExtensionSubsystem.h"
#include "UObject/UnrealType.h"

namespace
{
UModExtensionSubsystem* HostFor(UObject* Source)
{
	UWorld* World = IsInGameThread() && IsValid(Source) ? Source->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UModExtensionSubsystem>() : nullptr;
}
template <class T> int32 Invoke(UObject* Source, FName Id, T Value, T& Result, bool& bCancelled, bool& bCommitted)
{
	auto* Host = HostFor(Source);
	const auto Call = Host ? Host->InvokeBlueprintScalar<T>(*Source, FName(TEXT("hook/") + Id.ToString()), Value)
	                       : Mod::Extension::THookResult<T>{T{}, Mod::API::EError::NotReady};
	Result = Call.Value;
	bCancelled = Call.bCancelled;
	bCommitted = Call.bCommitted;
	return static_cast<int32>(Call.Error);
}

template <class T> int32 Publish(UObject* Source, FName Id, T Value)
{
	auto* Host = HostFor(Source);
	return static_cast<int32>(
	    Host ? Host->PublishBlueprintScalar<T>(*Source, FName(TEXT("event/") + Id.ToString()), Value)
	         : Mod::API::EError::NotReady);
}
} // namespace

int32 UModHostExtensionLibrary::HookPoint(UObject* Source, FName Id, float Value, float& Result, bool& bCancelled,
                                          bool& bCommitted)
{
	return Invoke(Source, Id, Value, Result, bCancelled, bCommitted);
}

int32 UModHostExtensionLibrary::PublishEvent(UObject* Source, FName Id, float Value)
{
	return Publish(Source, Id, Value);
}

int32 UModHostExtensionLibrary::HookPointDouble(UObject* Source, FName Id, double Value, double& Result,
                                                bool& bCancelled, bool& bCommitted)
{
	return Invoke(Source, Id, Value, Result, bCancelled, bCommitted);
}

int32 UModHostExtensionLibrary::PublishEventDouble(UObject* Source, FName Id, double Value)
{
	return Publish(Source, Id, Value);
}

int32 UModHostExtensionLibrary::HookPointInt32(UObject* Source, FName Id, int32 Value, int32& Result, bool& bCancelled,
                                               bool& bCommitted)
{
	return Invoke(Source, Id, Value, Result, bCancelled, bCommitted);
}

int32 UModHostExtensionLibrary::PublishEventInt32(UObject* Source, FName Id, int32 Value)
{
	return Publish(Source, Id, Value);
}

int32 UModHostExtensionLibrary::HookPointInt64(UObject* Source, FName Id, int64 Value, int64& Result, bool& bCancelled,
                                               bool& bCommitted)
{
	return Invoke(Source, Id, Value, Result, bCancelled, bCommitted);
}

int32 UModHostExtensionLibrary::PublishEventInt64(UObject* Source, FName Id, int64 Value)
{
	return Publish(Source, Id, Value);
}

int32 UModHostExtensionLibrary::HookPointBool(UObject* Source, FName Id, bool Value, bool& Result, bool& bCancelled,
                                              bool& bCommitted)
{
	return Invoke(Source, Id, Value, Result, bCancelled, bCommitted);
}

int32 UModHostExtensionLibrary::PublishEventBool(UObject* Source, FName Id, bool Value)
{
	return Publish(Source, Id, Value);
}

int32 UModHostExtensionLibrary::HookPointByte(UObject* Source, FName Id, uint8 Value, uint8& Result, bool& bCancelled,
                                              bool& bCommitted)
{
	return Invoke(Source, Id, Value, Result, bCancelled, bCommitted);
}

int32 UModHostExtensionLibrary::PublishEventByte(UObject* Source, FName Id, uint8 Value)
{
	return Publish(Source, Id, Value);
}

int32 UModHostExtensionLibrary::HookPointStruct(UObject*, FName, const int32&, int32&, bool&, bool&)
{
	return static_cast<int32>(Mod::API::EError::InvalidArgument);
}

DEFINE_FUNCTION(UModHostExtensionLibrary::execHookPointStruct)
{
	P_GET_OBJECT(UObject, Source);
	P_GET_PROPERTY(FNameProperty, Id);
	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	const auto* Input = CastField<FStructProperty>(Stack.MostRecentProperty);
	const void* Value = Stack.MostRecentPropertyAddress;
	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	const auto* Output = CastField<FStructProperty>(Stack.MostRecentProperty);
	void* Result = Stack.MostRecentPropertyAddress;
	P_GET_UBOOL_REF(bCancelled);
	P_GET_UBOOL_REF(bCommitted);
	P_FINISH;
	P_NATIVE_BEGIN;
	using namespace Mod::Extension;
	auto Error = Mod::API::EError::InvalidArgument;
	bCancelled = bCommitted = false;
	if (Input && Output && Input->Struct == Output->Struct && Value && Result)
	{
		Error = Mod::API::EError::NotReady;
		if (auto* Host = HostFor(Source))
		{
			const auto Call = Host->InvokeBlueprintPayload(*Source, FName(TEXT("hook/") + Id.ToString()),
			                                               FPayload::FromStruct(*Input->Struct, Value));
			Error = Call.Error;
			bCancelled = Call.bCancelled;
			bCommitted = Call.bCommitted;
			if (Call.Value.GetStruct().GetScriptStruct() == Output->Struct)
			{
				Output->Struct->CopyScriptStruct(Result, Call.Value.GetStruct().GetMemory());
			}
		}
	}
	*static_cast<int32*>(RESULT_PARAM) = static_cast<int32>(Error);
	P_NATIVE_END;
}

int32 UModHostExtensionLibrary::PublishEventStruct(UObject*, FName, const int32&)
{
	return static_cast<int32>(Mod::API::EError::InvalidArgument);
}

int32 UModHostExtensionLibrary::HookPointObject(UObject* Source, FName Id, UObject* Value, UObject*& Result,
                                                bool& bCancelled, bool& bCommitted)
{
	using namespace Mod::Extension;
	auto* Host = HostFor(Source);
	const auto Call =
	    Host ? Host->InvokeBlueprintPayload(*Source, FName(TEXT("hook/") + Id.ToString()), FPayload::FromObject(Value))
	         : THookResult<FPayload>{{}, Mod::API::EError::NotReady};
	Result = const_cast<UObject*>(Call.Value.GetObject());
	bCancelled = Call.bCancelled;
	bCommitted = Call.bCommitted;
	return static_cast<int32>(Call.Error);
}

int32 UModHostExtensionLibrary::PublishEventObject(UObject* Source, FName Id, UObject* Value)
{
	return Publish(Source, Id, Mod::Extension::FPayload::FromObject(Value));
}

DEFINE_FUNCTION(UModHostExtensionLibrary::execPublishEventStruct)
{
	P_GET_OBJECT(UObject, Source);
	P_GET_PROPERTY(FNameProperty, Id);
	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	const auto* Input = CastField<FStructProperty>(Stack.MostRecentProperty);
	const void* Value = Stack.MostRecentPropertyAddress;
	P_FINISH;
	P_NATIVE_BEGIN;
	using namespace Mod::Extension;
	const auto Error = Input && Value ? Publish(Source, Id, FPayload::FromStruct(*Input->Struct, Value))
	                                  : static_cast<int32>(Mod::API::EError::InvalidArgument);
	*static_cast<int32*>(RESULT_PARAM) = Error;
	P_NATIVE_END;
}
