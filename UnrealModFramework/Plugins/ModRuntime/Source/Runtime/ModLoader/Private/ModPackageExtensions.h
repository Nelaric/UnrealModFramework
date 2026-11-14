// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "ModLoader.h"

class FJsonObject;

namespace Mod::Loader::Private
{
bool ReadExtensions(const FJsonObject& Manifest, FLoadResult& Result);
}
