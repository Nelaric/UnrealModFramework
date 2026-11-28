// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "CoreMinimal.h"

#if PLATFORM_WINDOWS
namespace Mod::Extension
{
bool ValidateNativeImports(const FString& Path, FString& Error);
} // namespace Mod::Extension
#endif
