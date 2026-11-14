// Copyright (c) 2026 Nelaric Contributors
#include "ModExtensionLibrary.h"

#include "ModInstance.h"

namespace
{
UModExtensionLibrary::FBridge ExtensionBridge{};
}

int32 UModExtensionLibrary::RegisterModExtension(UModInstance* Owner, FName Id, FName Key, FName Function,
                                                 EModExtensionStage Stage, EModExtensionOperation Operation,
                                                 int32 Priority, const TArray<FString>& Before,
                                                 const TArray<FString>& After, float CancelValue,
                                                 FModExtensionToken& Token)
{
	Token = {};
	return IsInGameThread() && IsValid(Owner) && ExtensionBridge.Register
	           ? ExtensionBridge.Register(*Owner, Id, Key, Function, Stage, Operation, Priority, Before, After,
	                                      CancelValue, Token)
	           : -1;
}

void UModExtensionLibrary::UnregisterModExtension(UModInstance* Owner, const FModExtensionToken& Token)
{
	if (IsInGameThread() && IsValid(Owner) && ExtensionBridge.Unregister)
	{
		ExtensionBridge.Unregister(*Owner, Token);
	}
}

int32 UModExtensionLibrary::ProvideModService(UModInstance* Owner, FName Id, FName TypeId, const FString& Signature,
                                              const FString& Version, FName Permission, UObject* Instance)
{
	return IsInGameThread() && IsValid(Owner) && ExtensionBridge.Provide
	           ? ExtensionBridge.Provide(*Owner, Id, TypeId, Signature, Version, Permission, Instance)
	           : -1;
}

void UModExtensionLibrary::WithdrawModService(UModInstance* Owner, FName Id)
{
	if (IsInGameThread() && IsValid(Owner) && ExtensionBridge.Withdraw)
	{
		ExtensionBridge.Withdraw(*Owner, Id);
	}
}

bool UModExtensionLibrary::AcquireModService(UModInstance* Owner, FName Id, FName TypeId, const FString& Signature,
                                             int64& Context, int64& Object)
{
	Context = 0;
	Object = 0;
	uint64 NativeContext = 0;
	uint64 NativeObject = 0;
	if (!IsInGameThread() || !IsValid(Owner) ||
	    !Owner->AcquireModService(Id, TypeId, *Signature, NativeContext, NativeObject))
	{
		return false;
	}
	Context = static_cast<int64>(NativeContext);
	Object = static_cast<int64>(NativeObject);
	return true;
}

void UModExtensionLibrary::SetBridge(FBridge Bridge)
{
	check(IsInGameThread());
	ExtensionBridge = Bridge;
}
