// Copyright (c) 2026 Nelaric Contributors
/** @file ModExtensionSubsystem.h */
#pragma once

#include "ModAPIObject.h"
#include "ModExtension.h"
#include "ModPackageExtensions.h"
#include "Subsystems/WorldSubsystem.h"

#include "ModExtensionSubsystem.generated.h"

class ULevel;
class UModInstance;
struct FModExtensionWorld;

namespace Mod::Extension
{
/// A stable key for content selected by a game-defined slot.
using FContentKey = TPair<FName, FName>;

/// The result of resolving a logical content ID.
enum class EContentStatus : uint8
{
	/// The content could not resolve.
	Missing,
	/// The selected action is disable.
	Disabled,
	/// The selected asset is ready.
	Available,
};

/// The winning content and its provenance, valid in the current World.
struct FResolvedContent
{
	EContentStatus Status = EContentStatus::Missing;
	FString ModId;
	FSoftObjectPath Path;
	TArray<FString> Candidates;
};

/// Host rules for one slot, including its base game defaults.
struct FContentSlot
{
	UClass* AssetType = nullptr;
	TMap<FName, FSoftObjectPath> Defaults;
	bool bAllowProvide = true;
	bool bAllowReplace = false;
	bool bAllowDisable = false;
	bool bRequired = true;
};

/// Game-owned authorization and conflict choices for one World.
struct FWorldPolicy
{
	TMap<FString, TSet<FName>> Grants;
	/// Restricts participation when the set is populated.
	TSet<FString> EnabledMods;
	TMap<FContentKey, FString> ContentWinners;
};

/// Describes a precompiled cross-Mod service supplied by one Mod instance.
struct FService
{
	FName Id;
	FName TypeId;
	FString Version;
	FString Signature;
	FName Permission;
};

} // namespace Mod::Extension

/// Prepares and owns the extension plan for a single game World.
UCLASS(MinimalAPI)
class UModExtensionSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** @brief Defines a content slot on the game thread before Prepare.
	 * @param Id Stable slot identifier.
	 * @param Slot Host defaults and allowed operations.
	 * @return Whether the slot was installed.
	 */
	MODAPI_API bool DefineContentSlot(FName Id, Mod::Extension::FContentSlot Slot);
	/** @brief Registers an anchor on the game thread.
	 * @param Id Stable anchor identifier.
	 * @param Level Owning level in this World.
	 * @param Transform The spawn transform for admitted patches.
	 * @param Permission Host grant required to use the anchor.
	 * @return Whether the anchor and required patches were accepted.
	 */
	MODAPI_API bool RegisterAnchor(FName Id, ULevel& Level, const FTransform& Transform, FName Permission);
	/// Releases one anchor's effects at a game-thread safe point.
	MODAPI_API void RemoveAnchor(FName Id, ULevel& Level);
	/** @brief Prepares mounted extensions on the game thread.
	 * @param Policy Grants and explicit content winner choices for this World.
	 * @param OutError Failure reason when preparation closes the World plan.
	 * @return Whether the game can open its gameplay gate.
	 */
	MODAPI_API bool Prepare(Mod::Extension::FWorldPolicy Policy, FString& OutError);
	/// Reports whether this World may start extension-aware gameplay.
	MODAPI_API bool IsReady() const;
	/// Reads the selected content and its source on the game thread.
	MODAPI_API Mod::Extension::FResolvedContent ResolveContent(FName Slot, FName Id) const;
	/// Registers a callback for the current mod and returns its revocation token.
	MODAPI_API TModResult<Mod::Extension::FSubscription>
	Register(UModInstance& Owner, FName Id, const FString& Signature, Mod::Extension::FHandler Handler);
	/// Requests ordinary unsubscription at the next game-thread safe point.
	MODAPI_API void Unregister(UModInstance& Owner, const Mod::Extension::FSubscription& Token);
	/// Publishes a service from this mod's registration callback.
	MODAPI_API TModResult<void> Provide(UModInstance& Owner, const Mod::Extension::FService& Service,
	                                    UObject* Instance);
	/// Revokes the named service owned by this mod on the game thread.
	MODAPI_API void Withdraw(UModInstance& Owner, FName Id);
	/// Obtains a generated, revocable service binding on the game thread.
	MODAPI_API TModResult<Mod::API::FObjectBinding> Bind(UModInstance& Consumer, FName Id, FName TypeId,
	                                                     const TCHAR* Signature);
	/// Stops entry and releases resources once in-flight callbacks return.
	MODAPI_API void Stop();

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	static MODAPI_API Mod::Extension::FHookResult Invoke(UObject& Source, FName Id, Mod::Extension::FEndpoint& Endpoint,
	                                                     float Value,
	                                                     TFunctionRef<Mod::Extension::FCoreResult(float)> Core,
	                                                     TFunctionRef<bool(float)> ValidateInput,
	                                                     TFunctionRef<bool(float)> ValidateResult);
	static MODAPI_API Mod::API::EError Emit(UObject& Source, FName Id, Mod::Extension::FEndpoint& Endpoint,
	                                        float Value);
	MODAPI_API Mod::Extension::FHookResult InvokeBlueprint(UObject& Source, FName Id, float Value);
	MODAPI_API Mod::API::EError PublishBlueprint(UObject& Source, FName Id, float Value);
	bool AcquireNativeService(UModInstance& Consumer, FName Id, FName TypeId, const TCHAR* Signature,
	                          uint64& OutContext, uint64& OutObject);
	MODAPI_API TModResult<Mod::Extension::FSubscription>
	RegisterBlueprint(UModInstance& Owner, FName Id, FName Key, FName Function, Mod::Extension::EStage Stage,
	                  Mod::Extension::EOperation Operation, Mod::Extension::FOrder Order, float CancelValue);

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	void OnWorldTearDown(UWorld* World);
	void OnLevelRemoved(ULevel* Level, UWorld* World);
	TSharedPtr<FModExtensionWorld> State;
	FDelegateHandle TearDownHandle;
	FDelegateHandle LevelRemovedHandle;
};
