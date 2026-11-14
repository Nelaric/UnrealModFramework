// Copyright (c) 2026 Nelaric Contributors
#include "ModHostExtensionLibrary.h"

#include "Engine/World.h"
#include "ModExtensionSubsystem.h"

namespace
{
UModExtensionSubsystem* HostFor(UObject* Source)
{
	UWorld* World = IsInGameThread() && IsValid(Source) ? Source->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UModExtensionSubsystem>() : nullptr;
}
} // namespace

int32 UModHostExtensionLibrary::HookPoint(UObject* Source, FName Id, float Value, float& Result, bool& bCancelled,
                                          bool& bCommitted)
{
	auto* Host = HostFor(Source);
	const auto Call = Host ? Host->InvokeBlueprint(*Source, FName(TEXT("hook/") + Id.ToString()), Value)
	                       : Mod::Extension::FHookResult{0, Mod::API::EError::NotReady};
	Result = Call.Value;
	bCancelled = Call.bCancelled;
	bCommitted = Call.bCommitted;
	return static_cast<int32>(Call.Error);
}

int32 UModHostExtensionLibrary::PublishEvent(UObject* Source, FName Id, float Value)
{
	auto* Host = HostFor(Source);
	return static_cast<int32>(Host ? Host->PublishBlueprint(*Source, FName(TEXT("event/") + Id.ToString()), Value)
	                               : Mod::API::EError::NotReady);
}
