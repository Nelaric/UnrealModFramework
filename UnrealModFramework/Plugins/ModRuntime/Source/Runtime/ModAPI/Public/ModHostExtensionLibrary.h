// Copyright (c) 2026 Nelaric Contributors
/** @file ModHostExtensionLibrary.h */
#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "ModHostExtensionLibrary.generated.h"

/// Cooked entry points referenced by the host Blueprint compiler.
UCLASS(MinimalAPI)
class UModHostExtensionLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/// Executes the generated Hook for a host Blueprint on the game thread.
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Source"))
	static int32 HookPoint(UObject* Source, FName Id, float Value, float& Result, bool& bCancelled, bool& bCommitted);

	/// Publishes a generated Event for its declaring Blueprint on the game thread.
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Source"))
	static int32 PublishEvent(UObject* Source, FName Id, float Value);

public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Source"))
	static int32 HookPointDouble(UObject* Source, FName Id, double Value, double& Result, bool& bCancelled,
	                             bool& bCommitted);
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Source"))
	static int32 PublishEventDouble(UObject* Source, FName Id, double Value);

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Source"))
	static int32 HookPointInt32(UObject* Source, FName Id, int32 Value, int32& Result, bool& bCancelled,
	                            bool& bCommitted);
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Source"))
	static int32 PublishEventInt32(UObject* Source, FName Id, int32 Value);

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Source"))
	static int32 HookPointInt64(UObject* Source, FName Id, int64 Value, int64& Result, bool& bCancelled,
	                            bool& bCommitted);
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Source"))
	static int32 PublishEventInt64(UObject* Source, FName Id, int64 Value);

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Source"))
	static int32 HookPointBool(UObject* Source, FName Id, bool Value, bool& Result, bool& bCancelled, bool& bCommitted);
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Source"))
	static int32 PublishEventBool(UObject* Source, FName Id, bool Value);

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Source"))
	static int32 HookPointByte(UObject* Source, FName Id, uint8 Value, uint8& Result, bool& bCancelled,
	                           bool& bCommitted);
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Source"))
	static int32 PublishEventByte(UObject* Source, FName Id, uint8 Value);

	UFUNCTION(BlueprintCallable, CustomThunk,
	          meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Source",
	                  CustomStructureParam = "Value,Result"))
	static int32 HookPointStruct(UObject* Source, FName Id, const int32& Value, int32& Result, bool& bCancelled,
	                             bool& bCommitted);
	DECLARE_FUNCTION(execHookPointStruct);

	UFUNCTION(BlueprintCallable, CustomThunk,
	          meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Source", CustomStructureParam = "Value"))
	static int32 PublishEventStruct(UObject* Source, FName Id, const int32& Value);
	DECLARE_FUNCTION(execPublishEventStruct);

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Source"))
	static int32 HookPointObject(UObject* Source, FName Id, UObject* Value, UObject*& Result, bool& bCancelled,
	                             bool& bCommitted);
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Source"))
	static int32 PublishEventObject(UObject* Source, FName Id, UObject* Value);
};
