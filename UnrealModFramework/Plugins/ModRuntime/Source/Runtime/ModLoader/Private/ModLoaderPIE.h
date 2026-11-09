// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "ModLoader.h"

#if WITH_EDITOR
namespace Mod::Loader::Private
{
bool IsPIEActive();
bool IsCurrentPIECompletion();
TArray<FLoadResult> GetPIEStartupResults();
} // namespace Mod::Loader::Private
#endif
