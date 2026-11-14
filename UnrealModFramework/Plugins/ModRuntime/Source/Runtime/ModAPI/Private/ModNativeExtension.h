// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "ModExtensionABI.h"
#include "ModExtension.h"
#include "ModInstance.h"

#include "ModNativeExtension.generated.h"

struct FModExtensionWorld;
namespace Mod::Loader
{
struct FMountedPackage;
}

UCLASS(MinimalAPI, Transient)
class UModNativeExtensionInstance final : public UModInstance
{
	GENERATED_BODY()
};

namespace Mod::Extension
{
class FNativeExtension final : public TSharedFromThis<FNativeExtension>
{
public:
	~FNativeExtension();
	bool Open(const Mod::Loader::FMountedPackage& Package, UModNativeExtensionInstance& Instance,
	          TSharedRef<FModExtensionWorld> World, FString& Error);
	void Close();

private:
	struct FCallback;
	uint32 Add(const UMF_ExtensionRegistrationV1& Registration, uint64& Token);
	uint32 Remove(uint64 Token);
	static uint32 UMF_EXTENSION_CALL Register(uint64 Session, const UMF_ExtensionRegistrationV1* Registration,
	                                          uint64* Token);
	static uint32 UMF_EXTENSION_CALL Unregister(uint64 Session, uint64 Token);
	static uint32 UMF_EXTENSION_CALL Next(uint64 Token, const void* Input, uint32 Size, void* Output,
	                                      uint32* Committed);
	static void* UMF_EXTENSION_CALL FindHostQuery(const char* Module, const char* Symbol);

	TWeakPtr<FModExtensionWorld> World;
	TWeakObjectPtr<UModNativeExtensionInstance> Owner;
	TMap<uint64, FSubscription> Tokens;
	UMF_ExtensionSessionV1 Shutdown{};
	uint64 SessionId = 0;
	void* Library = nullptr;
};
} // namespace Mod::Extension
