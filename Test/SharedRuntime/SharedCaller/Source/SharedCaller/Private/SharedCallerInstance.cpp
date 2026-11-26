// Copyright (c) 2026 Nelaric Contributors
#include "SharedCallerInstance.h"

#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, SharedCaller, "SharedCaller");

float USharedCallerInstance::ReadHealth(const FUMFDevSDKPlayerContext& PlayerContext)
{
	FModObject_T_f2e0514f60055d77 Character;
	EUMFDevSDKCallOutcome Outcome;
	int64 ErrorCode;
	UUMFDevSDKLibrary::GetLocalCharacter(PlayerContext, Character, Outcome, ErrorCode);
	float Value = -1.f;
	if (Outcome == EUMFDevSDKCallOutcome::Succeeded)
	{
		UUMFDevSDKLibrary::GetCurrentHealth(Character, Value, Outcome, ErrorCode);
	}
	return Outcome == EUMFDevSDKCallOutcome::Succeeded ? Value : -1.f;
}
