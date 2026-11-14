// Copyright (c) 2026 Nelaric Contributors
/** @file ModExtensionSubsystem.h */
#pragma once

#include "ModAPIObject.h"
#include "ModExtension.h"
#include "ModLoader.h"
#include "Subsystems/WorldSubsystem.h"

#include "ModExtensionSubsystem.generated.h"

class UDataAsset;
class ULevel;
class UModInstance;
class USceneComponent;
struct FModExtensionWorld;

namespace Mod::Extension
{
class FModNetworkHandshake;
class FModExtensionSave;

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
	/// Whether the game supplied the asset after the selected asset failed.
	bool bUsedFallback = false;
};

/// Host rules for one slot, including its base game defaults.
struct FContentSlot
{
	UClass* AssetType = nullptr;
	TMap<FName, FSoftObjectPath> Defaults;
	bool bAllowProvide = true;
	bool bAllowReplace = false;
	/// Allows replacement of an ID supplied by a declared dependency.
	bool bAllowReplaceProvided = false;
	bool bAllowDisable = false;
	bool bRequired = true;
	/// Optional host asset used when an optional winner cannot be loaded.
	FSoftObjectPath Fallback;
	/// Applies game-specific rules to loaded assets on the game thread.
	TFunction<bool(const UObject&)> ValidateAsset;
	/// Loads optional content on first resolution instead of at the gate.
	bool bLoadOnDemand = false;
};

/// Game-owned authorization and conflict choices for one World.
struct FWorldPolicy
{
	TMap<FString, TSet<FName>> Grants;
	/// Restricts participation when the set is populated.
	TSet<FString> EnabledMods;
	TMap<FContentKey, FString> ContentWinners;
	/// An explicit filter also supports a session with zero enabled mods.
	Mod::Loader::FPackageSelection Selection;
	/// Optional authoritative fingerprint to match before opening gameplay.
	FString ExpectedGameplayDigest;
	/// Resolves an admitted archive to a stable gameplay release ID.
	TFunction<FString(const Mod::Loader::FMountedPackage&)> GameplayReleaseId;
	/// Optional host admission check before loading extension resources.
	TFunction<bool(FString&)> ValidatePreparation;
	/// Stages validated save data against resolved logical content.
	TFunction<bool(FString&)> StageSavedState;
};

/// Publication state of a subscription token.
enum class ESubscriptionStatus : uint8
{
	/// The token has expired, was removed, or its batch was rejected.
	Inactive,
	/// The requested handler is waiting for the dispatch boundary.
	Pending,
	/// The handler belongs to the current published snapshot.
	Active,
};

/// Host-owned operations enabled for an anchor.
struct FPatchAnchorRules
{
	/// Authorizes spawning a Mod Actor in this anchor's level.
	bool bAllowSpawnActor = true;
	/// Optional registered scene component approved as an attachment parent.
	TWeakObjectPtr<USceneComponent> Attachment;
	/// Optional UDataAsset subclass accepted by the anchor's data registry.
	UClass* DataAssetType = nullptr;
};

/// Provenance for an effect created by the current World Patch plan.
struct FPatchOrigin
{
	/// Package that requested the Actor.
	FString ModId;
	/// ID declared by that package.
	FName PatchId;
	/// Exact archive SHA-256 for the applied revision.
	FString Revision;
	/// Host-defined anchor identity.
	FName Anchor;
	/// Generation of the World that produced the Actor.
	uint64 WorldGeneration = 0;
	/// Generation of the anchor activation that owns the effect.
	uint64 AnchorGeneration = 0;
};

/// An asset registered by an active World Patch.
struct FPatchData
{
	/// Cooked asset held while the registration is active.
	TWeakObjectPtr<UDataAsset> Asset;
	/// Owning package revision and anchor activation.
	FPatchOrigin Origin;
};

/// Describes why an extension World lost its gameplay gate.
struct FWorldFailure
{
	/// Stable reason for closing the plan.
	Mod::API::EError Error = Mod::API::EError::None;
	/// Human-readable origin and failure details.
	FString Detail;
	/// A new process is required to release uncertain state.
	bool bRestartRequired = false;
};

/// Receives an extension failure after gameplay entry points are closed.
DECLARE_DELEGATE_OneParam(FOnWorldFailure, const FWorldFailure&);

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
	 * @param Rules Effects the host allows at this anchor.
	 * @return Whether the anchor and required patches were accepted.
	 */
	MODAPI_API bool RegisterAnchor(FName Id, ULevel& Level, const FTransform& Transform, FName Permission,
	                               Mod::Extension::FPatchAnchorRules Rules = {});
	/// Releases one anchor's effects at a game-thread safe point.
	MODAPI_API void RemoveAnchor(FName Id, ULevel& Level);
	/// Reports the provenance of a tracked Patch Actor or component.
	MODAPI_API TOptional<Mod::Extension::FPatchOrigin> GetPatchOrigin(const UObject& Effect) const;
	/// Reads active Patch data and provenance on the game thread.
	MODAPI_API Mod::Extension::FPatchData ResolvePatchData(FName Anchor, FName DataId) const;
	/** @brief Prepares mounted extensions on the game thread.
	 * @param Policy Grants and explicit content winner choices for this World.
	 * @param OutError Failure reason when preparation closes the World plan.
	 * @return Whether the game can open its gameplay gate.
	 */
	MODAPI_API bool Prepare(Mod::Extension::FWorldPolicy Policy, FString& OutError);
	/// Reports whether this World may start extension-aware gameplay.
	MODAPI_API bool IsReady() const;
	/// Returns the published plan digest on the game thread, or an empty ID.
	MODAPI_API FString GetGameplayDigest() const;
	/// Copies the selected package revisions for game save metadata.
	MODAPI_API TMap<FString, FString> GetPackageRevisions() const;
	/// Returns the terminal extension failure on the game thread, if any.
	MODAPI_API Mod::Extension::FWorldFailure GetFailure() const;
	/// Installs a game-thread callback for the terminal failure of this World.
	MODAPI_API void SetFailureHandler(Mod::Extension::FOnWorldFailure Handler);
	/// Returns whether this World accepted an authoritative plan digest.
	MODAPI_API bool MatchesGameplayDigest(const FString& Digest) const;
	/// Reads the selected content and its source on the game thread.
	MODAPI_API Mod::Extension::FResolvedContent ResolveContent(FName Slot, FName Id) const;
	/// Registers a callback for the current mod and returns its revocation token.
	MODAPI_API TModResult<Mod::Extension::FSubscription>
	Register(UModInstance& Owner, FName Id, const FString& Signature, Mod::Extension::FHandler Handler);
	/// Requests ordinary unsubscription at the next game-thread safe point.
	MODAPI_API void Unregister(UModInstance& Owner, const Mod::Extension::FSubscription& Token);
	/// Reports the token's publication state on the game thread.
	MODAPI_API Mod::Extension::ESubscriptionStatus
	GetSubscriptionStatus(const UModInstance& Owner, const Mod::Extension::FSubscription& Token) const;
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
	template <class T>
	static MODAPI_API Mod::Extension::THookResult<T>
	InvokeScalar(UObject& Source, FName Id, Mod::Extension::FEndpoint& Endpoint, T Value,
	             TFunctionRef<Mod::Extension::TCoreResult<T>(T)> Core, TFunctionRef<bool(T)> ValidateInput,
	             TFunctionRef<bool(T)> ValidateResult);
	template <class T>
	static MODAPI_API Mod::API::EError EmitScalar(UObject& Source, FName Id, Mod::Extension::FEndpoint& Endpoint,
	                                              T Value);
	MODAPI_API Mod::Extension::FHookResult InvokeBlueprint(UObject& Source, FName Id, float Value);
	template <class T> Mod::Extension::THookResult<T> InvokeBlueprintScalar(UObject& Source, FName Id, T Value);
	template <class T> Mod::API::EError PublishBlueprintScalar(UObject& Source, FName Id, T Value);
	MODAPI_API Mod::API::EError PublishBlueprint(UObject& Source, FName Id, float Value);
	Mod::Extension::THookResult<Mod::Extension::FPayload> InvokeBlueprintPayload(UObject& Source, FName Id,
	                                                                             Mod::Extension::FPayload Value);
	bool AcquireNativeService(UModInstance& Consumer, FName Id, FName TypeId, const TCHAR* Signature,
	                          uint64& OutContext, uint64& OutObject);
	MODAPI_API TModResult<Mod::Extension::FSubscription>
	RegisterBlueprint(UModInstance& Owner, FName Id, FName Key, FName Function, Mod::Extension::EStage Stage,
	                  Mod::Extension::EOperation Operation, Mod::Extension::FOrder Order, float CancelValue);

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	friend class Mod::Extension::FModNetworkHandshake;
	friend class Mod::Extension::FModExtensionSave;
	void OnWorldTearDown(UWorld* World);
	void OnWorldTickStart(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void OnLevelRemoved(ULevel* Level, UWorld* World);
	TSharedPtr<FModExtensionWorld> State;
	FDelegateHandle TearDownHandle;
	FDelegateHandle TickHandle;
	FDelegateHandle LevelRemovedHandle;
};
