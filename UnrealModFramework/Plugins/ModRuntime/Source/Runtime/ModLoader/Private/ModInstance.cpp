// Copyright (c) 2026 Nelaric Contributors
#include "ModInstance.h"

#include "ModLifecycleSubsystem.h"

#include "Engine/LatentActionManager.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ObjectKey.h"

namespace
{
struct FExtensionIdentity
{
	TWeakObjectPtr<UWorld> World;
	FString ModId;
};

TMap<FObjectKey, FExtensionIdentity> ExtensionIdentities;

UModInstance::FAcquireModService ServiceResolver = nullptr;
UModInstance::FAcquireWorldAPI WorldAPIResolver = nullptr;
UModInstance::FResolveHostAPI HostAPIResolver = nullptr;
UModInstance::FRevokeWorldAPIs WorldAPIRevoker = nullptr;
} // namespace

UWorld* UModInstance::GetWorld() const
{
	if (IsInGameThread())
	{
		if (const auto* Entry = ExtensionIdentities.Find(FObjectKey(this)))
		{
			return Entry->World.Get();
		}
	}
	const auto* GameInstance = GetTypedOuter<UGameInstance>();
	return GameInstance ? GameInstance->GetWorld() : nullptr;
}

void UModInstance::SetExtensionWorld(UModInstance& Instance, UWorld& World, FString ModId)
{
	check(IsInGameThread());
	ExtensionIdentities.Add(FObjectKey(&Instance), {&World, MoveTemp(ModId)});
}

void UModInstance::RegisterExtensions()
{
	check(IsInGameThread());
	OnModRegister();
}

void UModInstance::BindExtensions()
{
	check(IsInGameThread());
	OnModBind();
}

void UModInstance::ReleaseExtensions()
{
	check(IsInGameThread());
	if (!ExtensionIdentities.Contains(FObjectKey(this)))
	{
		return;
	}
	OnModBeforeUnload();
	if (UWorld* World = GetWorld())
	{
		World->GetLatentActionManager().RemoveActionsForObject(this);
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	RevokeWorldAPIs();
	ExtensionIdentities.Remove(FObjectKey(this));
}

FString UModInstance::GetModId() const
{
	check(IsInGameThread());
	if (const auto* Entry = ExtensionIdentities.Find(FObjectKey(this)))
	{
		return Entry->ModId;
	}
	const auto* Game = GetTypedOuter<UGameInstance>();
	const auto* Lifecycle = Game ? Game->GetSubsystem<UModLifecycleSubsystem>() : nullptr;
	const FString* Id = Lifecycle ? Lifecycle->FindModId(*this) : nullptr;
	return Id ? *Id : FString();
}

bool UModInstance::AcquireModService(FName ServiceId, FName TypeId, const TCHAR* Signature, uint64& OutContext,
                                     uint64& OutObject)
{
	OutContext = 0;
	OutObject = 0;
	return IsInGameThread() && ServiceResolver &&
	       ServiceResolver(*this, ServiceId, TypeId, Signature, OutContext, OutObject);
}

void UModInstance::SetServiceBridge(FAcquireModService Acquire)
{
	check(IsInGameThread());
	ServiceResolver = Acquire;
}

bool UModInstance::AcquireWorldAPI(FName TypeId, const TCHAR* Signature, uint64& OutContext, uint64& OutObject)
{
	OutContext = 0;
	OutObject = 0;
	return IsInGameThread() && WorldAPIResolver && WorldAPIResolver(*this, TypeId, Signature, OutContext, OutObject);
}

void UModInstance::SetHostAPIBridge(FAcquireWorldAPI Acquire, FResolveHostAPI Resolve, FRevokeWorldAPIs Revoke)
{
	check(IsInGameThread());
	WorldAPIResolver = Acquire;
	HostAPIResolver = Resolve;
	WorldAPIRevoker = Revoke;
}

void UModInstance::RevokeWorldAPIs()
{
	if (WorldAPIRevoker)
	{
		WorldAPIRevoker(*this);
	}
}

void* UModInstance::ResolveHostAPI(const char* Module, const char* Symbol)
{
	return IsInGameThread() && HostAPIResolver ? HostAPIResolver(Module, Symbol) : nullptr;
}
