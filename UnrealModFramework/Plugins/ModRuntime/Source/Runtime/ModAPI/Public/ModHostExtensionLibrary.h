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
};
