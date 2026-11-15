// Copyright (c) 2026 Nelaric Contributors
/** @file ModInstance.h */
#pragma once

#include "Engine/GameInstance.h"
#include "UObject/Object.h"

#include "ModInstance.generated.h"

/// Blueprint payload for one finished Mod load attempt.
USTRUCT(BlueprintType)
struct MODLOADER_API FModLoadNotification
{
	GENERATED_BODY()

	/// The identity read from the archive, when available.
	UPROPERTY(BlueprintReadOnly, Category = "Mod|Lifecycle")
	FString ModId;

	/// The version read from the archive, when available.
	UPROPERTY(BlueprintReadOnly, Category = "Mod|Lifecycle")
	FString Version;

	/// Name of the archive or of the directory that could not be scanned.
	UPROPERTY(BlueprintReadOnly, Category = "Mod|Lifecycle")
	FString InputName;

	/// Whether the mount succeeded or was already present.
	UPROPERTY(BlueprintReadOnly, Category = "Mod|Lifecycle")
	bool bSucceeded = false;

	/// Whether the exact archive was already present before this operation.
	UPROPERTY(BlueprintReadOnly, Category = "Mod|Lifecycle")
	bool bAlreadyMounted = false;

	/// Whether the result came from the configured startup scan.
	UPROPERTY(BlueprintReadOnly, Category = "Mod|Lifecycle")
	bool bStartup = false;

	/// Stable ModLoader error code, zero on success.
	UPROPERTY(BlueprintReadOnly, Category = "Mod|Lifecycle")
	int32 ErrorCode = 0;

	/// Failure reason supplied by ModLoader.
	UPROPERTY(BlueprintReadOnly, Category = "Mod|Lifecycle")
	FString Reason;
};

/// A game-thread Mod entry point owned by one game instance.
UCLASS(Abstract, BlueprintType, Blueprintable, MinimalAPI, meta = (DontUseGenericSpawnObject))
class UModInstance : public UObject
{
	GENERATED_BODY()

public:
	/// Returns the extension World or the World of the owning game instance.
	MODLOADER_API UWorld* GetWorld() const override;

	/** @brief Returns this instance's active package ID on the game thread.
	 * @return The package ID, or an empty string outside the active lifetime.
	 */
	MODLOADER_API FString GetModId() const;

	/** @brief Acquires a host-approved world provider on the game thread.
	 * @param TypeId Stable identity of the requested provider.
	 * @param Signature The caller's generated calling table signature.
	 * @param OutContext Receives the session-owned context token.
	 * @param OutObject Receives the non-owning provider token.
	 * @return True when the host authorized and issued both tokens.
	 */
	MODLOADER_API bool AcquireWorldAPI(FName TypeId, const TCHAR* Signature, uint64& OutContext, uint64& OutObject);

	/** @brief Resolves a host API table query on the game thread.
	 * @param Module Owning module in the generated SDK contract.
	 * @param Symbol Query symbol in that module.
	 * @return A borrowed symbol address, or null when unavailable.
	 */
	static MODLOADER_API void* ResolveHostAPI(const char* Module, const char* Symbol);

	/** @brief Acquires a declared cross-Mod service on the game thread.
	 * @param ServiceId A service required by the owning package.
	 * @param TypeId The generated service interface.
	 * @param Signature The caller's generated table signature.
	 * @param OutContext Receives a context owned by this Mod instance.
	 * @param OutObject Receives the revocable service token.
	 * @return True when the service is available and authorized.
	 */
	MODLOADER_API bool AcquireModService(FName ServiceId, FName TypeId, const TCHAR* Signature, uint64& OutContext,
	                                     uint64& OutObject);

public:
	using FAcquireModService = bool (*)(UModInstance&, FName, FName, const TCHAR*, uint64&, uint64&);
	static MODLOADER_API void SetServiceBridge(FAcquireModService Acquire);
	using FAcquireWorldAPI = bool (*)(UModInstance&, FName, const TCHAR*, uint64&, uint64&);
	using FResolveHostAPI = void* (*)(const char*, const char*);
	using FRevokeWorldAPIs = void (*)(UModInstance&);
	static MODLOADER_API void SetHostAPIBridge(FAcquireWorldAPI Acquire, FResolveHostAPI Resolve,
	                                           FRevokeWorldAPIs Revoke);

	static MODLOADER_API void SetExtensionWorld(UModInstance& Instance, UWorld& World, FString ModId);
	MODLOADER_API void RegisterExtensions();
	MODLOADER_API void BindExtensions();
	MODLOADER_API void ReleaseExtensions(bool bNotify = true);

protected:
	/** @brief Receives the result of a load operation on the game thread.
	 * @param Result The completed startup or
	 * manual load result.
	 * @details Call Super::OnModLoadCompleted to dispatch the Blueprint event.
	 */
	virtual void OnModLoadCompleted(const FModLoadNotification& Result)
	{
		ReceiveModLoadCompleted(Result);
	}

	/** @brief Blueprint event for a completed Mod load.
	 * @param Result The same load result passed to the C++
	 * entry point.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Mod|Lifecycle", meta = (DisplayName = "On Mod Load Completed"))
	MODLOADER_API void ReceiveModLoadCompleted(const FModLoadNotification& Result);

	/** @brief Releases game state and references before this Mod is unloaded.
	 * @details Runs once per instance on
	 * the game thread while the Mod's content
	 * is still mounted. Call Super::OnModBeforeUnload to dispatch the
	 * event.
	 */
	virtual void OnModBeforeUnload()
	{
		ReceiveModBeforeUnload();
	}

	/// Blueprint event dispatched before this Mod's content is unloaded.
	UFUNCTION(BlueprintImplementableEvent, Category = "Mod|Lifecycle", meta = (DisplayName = "On Mod Before Unload"))
	MODLOADER_API void ReceiveModBeforeUnload();

	/// Registers this mod's extensions while its World is preparing.
	UFUNCTION(BlueprintImplementableEvent, Category = "Mod|Lifecycle", meta = (DisplayName = "On Mod Register"))
	MODLOADER_API void ReceiveModRegister();

	/// Registers compiled C++ extensions before the World is ready.
	virtual void OnModRegister()
	{
		ReceiveModRegister();
	}

	/// Acquires declared services after all providers register.
	UFUNCTION(BlueprintImplementableEvent, Category = "Mod|Lifecycle", meta = (DisplayName = "On Mod Bind"))
	MODLOADER_API void ReceiveModBind();

	/// Binds compiled C++ consumers after all providers are registered.
	virtual void OnModBind()
	{
		ReceiveModBind();
	}

private:
	friend class UModLifecycleSubsystem;
	void RevokeWorldAPIs();
};
