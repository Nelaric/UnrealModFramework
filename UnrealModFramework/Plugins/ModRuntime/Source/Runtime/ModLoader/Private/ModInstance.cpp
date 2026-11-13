// Copyright (c) 2026 Nelaric Contributors
#include "ModInstance.h"

#include "ModLifecycleSubsystem.h"

namespace
{
UModInstance::FAcquireWorldAPI WorldAPIResolver = nullptr;
UModInstance::FResolveHostAPI HostAPIResolver = nullptr;
UModInstance::FRevokeWorldAPIs WorldAPIRevoker = nullptr;
} // namespace

FString UModInstance::GetModId() const
{
	check(IsInGameThread());
	const auto* Game = GetTypedOuter<UGameInstance>();
	const auto* Lifecycle = Game ? Game->GetSubsystem<UModLifecycleSubsystem>() : nullptr;
	const FString* Id = Lifecycle ? Lifecycle->FindModId(*this) : nullptr;
	return Id ? *Id : FString();
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
