// Copyright (c) 2026 Nelaric Contributors
/** @file ModHookTestWorld.h */
#pragma once

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Containers/Array.h"
#include "Containers/Set.h"
#include "Templates/Function.h"
#include "UObject/NameTypes.h"

class UModExtensionSubsystem;
class UModInstance;
class UWorld;

namespace Mod::Extension
{
struct FDefinition;
}

namespace Mod::Tests
{
/// Prepares an isolated Hook channel for host automation.
struct FHookTestWorld
{
	/// Runs the test using the normal World registration and dispatch path.
	static MODAPI_API bool Run(UWorld& World, TArray<Extension::FDefinition> Definitions, TSet<FName> Grants,
	                           TFunctionRef<bool(UModExtensionSubsystem&, UModInstance&)> Test);
};
} // namespace Mod::Tests

#endif
