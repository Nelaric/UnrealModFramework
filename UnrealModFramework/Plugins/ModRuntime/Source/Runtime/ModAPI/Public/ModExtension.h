// Copyright (c) 2026 Nelaric Contributors
/** @file ModExtension.h */
#pragma once

#include "ModAPIBlueprint.h"
#include "Templates/Function.h"
#include "UObject/ObjectKey.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"

class UModInstance;
struct FModExtensionWorld;

namespace Mod::Extension
{
/// Declaration kinds used by generated World extensions.
enum class EKind : uint8
{
	/// Surrounds a host operation.
	Hook,
	/// Publishes a gameplay fact.
	Event,
};

/// The synchronous stages at which Mods can participate.
enum class EStage : uint8
{
	/// Before original execution.
	Pre,
	/// After successful execution.
	Post,
	/// A read-only notification.
	Event,
};

/// Operations the host can grant for an individual Hook.
enum class EOperation : uint8
{
	/// Observation only.
	None = 0,
	/// Replaces the next input.
	ModifyInputs = 1 << 0,
	/// Returns a cancellation result.
	Cancel = 1 << 1,
	/// Replaces the returned value.
	ModifyResult = 1 << 2,
};
ENUM_CLASS_FLAGS(EOperation)

/// Stable definition and policy emitted by the export generator.
struct FDefinition
{
	FName Id;
	FString Version;
	FString Signature;
	EKind Kind = EKind::Event;
	bool bPre = false;
	bool bPost = false;
	EOperation Operations = EOperation::None;
	FName Permission;
	FName PublishPermission;
	FName ModifyInputsPermission;
	FName CancelPermission;
	FName ModifyResultPermission;
	bool bAuthorityOnly = true;
	FSoftClassPath BlueprintClass;
	FName BlueprintCore;
	FName BlueprintParameter;
	FName BlueprintResult;
	API::EBlueprintParameter BlueprintResultRole = API::EBlueprintParameter::Output;
};

/// How to order this registration relative to other participating Mods.
struct FOrder
{
	int32 Priority = 0;
	TArray<FString> Before;
	TArray<FString> After;
};

/// Status and complete result of a generated Hook call.
struct FHookResult
{
	float Value = 0;
	API::EError Error = API::EError::None;
	bool bCancelled = false;
	bool bCommitted = false;
};

/// Result of the host's original operation.
struct FCoreResult
{
	float Value = 0;
	API::EError Error = API::EError::None;
	bool bCommitted = false;
};

/// A handler's decision. Writes and cancellation require the declared grant.
struct FHookDecision
{
	TOptional<float> Value;
	API::EError Error = API::EError::None;
	bool bCancel = false;
};

/// One resolved handler bound to its original owner and exact signature.
struct FHandler
{
	TWeakObjectPtr<UModInstance> Owner;
	TWeakObjectPtr<UObject> Target;
	TSharedPtr<const void> Identity;
	FName Function;
	FString ModId;
	FName Key;
	FOrder Order;
	EStage Stage = EStage::Event;
	EOperation Operations = EOperation::None;
	TFunction<FHookDecision(float)> Hook;
	TFunction<API::EError(float)> Event;
	uint64 Serial = 0;
	float CancelValue = 0;
};

/// Revocation identity, valid only for the issuing World generation.
struct FSubscription
{
	FName Id;
	uint64 Generation = 0;
	uint64 Serial = 0;
};

// Sorts registration indices for the World preparation and dispatch paths.
bool SortExtensions(TConstArrayView<FHandler> Handlers, TArray<int32>& Out, FString& Detail);

/// Owns the staged and published handlers for one generated extension.
class FChannel
{
public:
	/// Takes ownership of an immutable generated definition.
	explicit FChannel(FDefinition InDefinition) : Definition(MoveTemp(InDefinition))
	{
	}

	/// Returns the registered definition.
	const FDefinition& GetDefinition() const
	{
		return Definition;
	}

public:
	TModResult<uint64> Add(FHandler Handler);
	void Remove(UModInstance& Owner, uint64 Serial);
	bool Build(FString& Detail);
	void Publish();
	void Discard();
	FHookResult Invoke(UObject& Source, float Value, TFunctionRef<FCoreResult(float)> Core,
	                   TFunctionRef<bool(float)> ValidateInput, TFunctionRef<bool(float)> ValidateResult);
	API::EError Emit(UObject& Source, float Value);

private:
	using FSnapshot = TArray<TSharedPtr<FHandler>>;
	TSharedPtr<const FSnapshot> Pre;
	TSharedPtr<const FSnapshot> Post;
	TSharedPtr<const FSnapshot> Events;
	TSharedPtr<const FSnapshot> PendingPre;
	TSharedPtr<const FSnapshot> PendingPost;
	TSharedPtr<const FSnapshot> PendingEvents;
	FSnapshot Desired;
	TSet<FObjectKey> Invoking;
	FDefinition Definition;
	uint64 LastSerial = 0;
	bool bPublishing = false;
};

/// A cached link from a host call site to the current World's channel.
struct FEndpoint
{
	TWeakObjectPtr<UWorld> World;
	TWeakPtr<FChannel> Channel;
	TWeakPtr<FModExtensionWorld> State;
	FName Id;
};

/// Installs generated definitions in a World before it can become ready.
class MODAPI_API FGeneratedRegistration
{
public:
	/// Appends a generated module's immutable definitions.
	using FDescribe = void (*)(TArray<FDefinition>&);

public:
	explicit FGeneratedRegistration(FDescribe InDescribe);
	~FGeneratedRegistration();
	static void DescribeAll(TArray<FDefinition>& Out);

private:
	static FGeneratedRegistration* First;
	FGeneratedRegistration* Next;
	FDescribe Describe;
};
} // namespace Mod::Extension
