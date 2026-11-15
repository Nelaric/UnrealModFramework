// Copyright (c) 2026 Nelaric Contributors
/** @file ModExtension.h */
#pragma once

#include "ModAPIBlueprint.h"
#include "ModExtensionPayload.h"
#include "Templates/Function.h"
#include "UObject/ObjectKey.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"

#include <type_traits>

class UModInstance;
struct FModExtensionWorld;

namespace Mod::Extension
{
/// By-value scalar formats shared by generated World contracts.
enum class EScalar : uint8
{
	/// IEEE single precision.
	Float,
	/// IEEE double precision.
	Double,
	/// Signed 32-bit integer.
	Int32,
	/// Signed 64-bit integer.
	Int64,
	/// Boolean.
	Bool,
	/// Unsigned byte.
	Byte,
	/// Reflected value or read-only object projection.
	Projected,
};

/// Gets the fixed format supported for the given UE scalar type.
template <class T> constexpr EScalar ScalarType()
{
	if constexpr (std::is_same_v<T, float>)
	{
		return EScalar::Float;
	}
	else if constexpr (std::is_same_v<T, double>)
	{
		return EScalar::Double;
	}
	else if constexpr (std::is_same_v<T, int32>)
	{
		return EScalar::Int32;
	}
	else if constexpr (std::is_same_v<T, int64>)
	{
		return EScalar::Int64;
	}
	else if constexpr (std::is_same_v<T, bool>)
	{
		return EScalar::Bool;
	}
	else if constexpr (std::is_same_v<T, FPayload>)
	{
		return EScalar::Projected;
	}
	else
	{
		static_assert(std::is_same_v<T, uint8>, "Unsupported Mod extension scalar type.");
		return EScalar::Byte;
	}
}

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
	/// Wraps or replaces the host operation.
	On,
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
	/// Wraps the original operation using a synchronous continuation.
	Wrap = 1 << 3,
	/// Supplies the sole Core replacement.
	Replace = 1 << 4,
};
ENUM_CLASS_FLAGS(EOperation)

/// Stable definition and policy emitted by the export generator.
struct FDefinition
{
	FName Id;
	FString Version;
	FString Signature;
	EKind Kind = EKind::Event;
	EScalar Scalar = EScalar::Float;
	FPayloadType Payload;
	bool bQueued = false;
	bool bImmediateUnsubscribe = false;
	bool bPre = false;
	bool bPost = false;
	bool bOn = false;
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
template <class T> struct THookResult
{
	T Value{};
	API::EError Error = API::EError::None;
	bool bCancelled = false;
	bool bCommitted = false;
};

/// Result of the host's original operation.
template <class T> struct TCoreResult
{
	T Value{};
	API::EError Error = API::EError::None;
	bool bCommitted = false;
};

/// A handler's decision. Writes and cancellation require the declared grant.
template <class T> struct THookDecision
{
	TOptional<T> Value;
	API::EError Error = API::EError::None;
	bool bCancel = false;
};

/// Float Hook result used by the original extension contract.
using FHookResult = THookResult<float>;
/// Float Core result used by the original extension contract.
using FCoreResult = TCoreResult<float>;
/// Float handler decision used by the original extension contract.
using FHookDecision = THookDecision<float>;

/// A copyable continuation, valid only during its wrapping callback.
template <class T> using TNext = TFunction<TCoreResult<T>(T)>;

/// Typed callbacks stored by a resolved scalar handler.
template <class T> struct THandlerCalls
{
	TFunction<THookDecision<T>(T)> Hook;
	TFunction<API::EError(T)> Event;
	TFunction<TCoreResult<T>(T, const TNext<T>&)> Wrap;
	TFunction<TCoreResult<T>(T)> Replace;
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
	TFunction<TModResult<FPayload>(const FPayload&, const TSharedRef<const uint8>&)> Project;
	EStage Stage = EStage::Event;
	EOperation Operations = EOperation::None;
	TFunction<FHookDecision(float)> Hook;
	TFunction<API::EError(float)> Event;
	uint64 Serial = 0;
	float CancelValue = 0;

public:
	/// Installs callbacks matching the contract's generated scalar type.
	template <class T> void SetCalls(THandlerCalls<T> Calls)
	{
		Hook = {};
		Event = {};
		Scalar = ScalarType<T>();
		bHasHook = static_cast<bool>(Calls.Hook);
		bHasEvent = static_cast<bool>(Calls.Event);
		bHasWrap = static_cast<bool>(Calls.Wrap);
		bHasReplace = static_cast<bool>(Calls.Replace);
		TypedCalls = MakeShared<THandlerCalls<T>>(MoveTemp(Calls));
	}

public:
	bool HasCallback() const
	{
		if (Stage == EStage::On)
		{
			return Operations == EOperation::Wrap ? bHasWrap : Operations == EOperation::Replace && bHasReplace;
		}
		return Stage == EStage::Event ? bHasEvent || Event : bHasHook || Hook;
	}

	bool UsesScalar(EScalar Expected) const
	{
		return Scalar == Expected;
	}

	template <class T> const THandlerCalls<T>* GetTypedCalls() const
	{
		return static_cast<const THandlerCalls<T>*>(TypedCalls.Get());
	}

private:
	EScalar Scalar = EScalar::Float;
	bool bHasHook = false;
	bool bHasEvent = false;
	bool bHasWrap = false;
	bool bHasReplace = false;
	TSharedPtr<const void> TypedCalls;
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
	void DescribeOrder(TArray<FString>& Out) const;
	bool Contains(const UModInstance& Owner, uint64 Serial, bool bPublished) const;
	template <class T>
	THookResult<T> Invoke(UObject& Source, T Value, TFunctionRef<TCoreResult<T>(T)> Core,
	                      TFunctionRef<bool(T)> ValidateInput, TFunctionRef<bool(T)> ValidateResult);
	template <class T> API::EError Emit(UObject& Source, T Value);

private:
	using FSnapshot = TArray<TSharedPtr<FHandler>>;
	TSharedPtr<const FSnapshot> Pre;
	TSharedPtr<const FSnapshot> Post;
	TSharedPtr<const FSnapshot> On;
	TSharedPtr<const FSnapshot> Events;
	TSharedPtr<const FSnapshot> PendingPre;
	TSharedPtr<const FSnapshot> PendingPost;
	TSharedPtr<const FSnapshot> PendingOn;
	TSharedPtr<const FSnapshot> PendingEvents;
	FSnapshot Desired;
	TSet<FObjectKey> Invoking;
	FDefinition Definition;
	uint64 LastSerial = 0;
	bool bPublishing = false;
	TSet<uint64> ImmediatelyRemoved;
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
