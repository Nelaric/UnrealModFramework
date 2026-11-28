// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "CoreMinimal.h"

#if PLATFORM_LINUX || PLATFORM_MAC
#include "ModNativeTarget.h"
#endif

namespace Mod::Extension
{
#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_MAC
bool ValidateNativeImports(const FString& Path, FString& Error);
#endif

#if PLATFORM_LINUX || PLATFORM_MAC
inline bool ReadNativeImportString(const Native::FImageFile& Image, uint64 Offset, uint64 Available, FString& Out)
{
	ANSICHAR Buffer[1024];
	const uint64 Count = FMath::Min<uint64>(Available, sizeof(Buffer));
	if (Count == 0 || !Image.ReadBytes(Offset, Buffer, Count))
	{
		return false;
	}
	for (uint64 Index = 0; Index < Count; ++Index)
	{
		const uint8 Char = static_cast<uint8>(Buffer[Index]);
		if (Char == 0)
		{
			Out = ANSI_TO_TCHAR(Buffer);
			return Index > 0;
		}
		if (Char < 0x20 || Char > 0x7e)
		{
			return false;
		}
	}
	return false;
}
#endif
} // namespace Mod::Extension
