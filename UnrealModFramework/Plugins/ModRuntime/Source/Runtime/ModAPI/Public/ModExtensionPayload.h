// Copyright (c) 2026 Nelaric Contributors
/** @file ModExtensionPayload.h */
#pragma once

#include "ModAPIObject.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "UObject/StrongObjectPtr.h"

class UWorld;

namespace Mod::Extension
{
/// One World payload with owned struct storage or a weak object identity.
class MODAPI_API FPayload
{
public:
	/// Creates an empty payload.
	FPayload() = default;
	/// Copies a value of the supplied struct type into owned storage.
	static FPayload FromStruct(const UScriptStruct& Type, const void* Value);
	/// Copies a reflected native struct into owned storage.
	template <class T> static FPayload FromStruct(const T& Value)
	{
		return FromStruct(*T::StaticStruct(), &Value);
	}
	/// Captures an object weakly for the duration of the World operation.
	static FPayload FromObject(const UObject* Value);
	/// Reads a copied struct while this payload remains alive.
	const FInstancedStruct& GetStruct() const
	{
		return Struct;
	}
	/// Projects an object to a revocable, read-only binding for this callback.
	TModResult<API::FObjectBinding> GetObjectView() const
	{
		return View ? TModResult<API::FObjectBinding>::Success(*View)
		            : TModResult<API::FObjectBinding>::Failure(API::EError::BindingExpired);
	}

public:
	const UObject* GetObject() const
	{
		return Object.Get();
	}
	bool IsNullObject() const
	{
		return Object.IsExplicitlyNull();
	}
	void SetView(API::FObjectBinding Binding)
	{
		View = MoveTemp(Binding);
	}

private:
	FInstancedStruct Struct;
	TWeakObjectPtr<const UObject> Object;
	TOptional<API::FObjectBinding> View;
};

/// The reflected identity and generated validation for a payload projection.
struct FPayloadType
{
	/// Exact script struct path, when used by the contract.
	FSoftObjectPath Struct;
	/// Exact UObject class path, when used by the contract.
	FSoftClassPath Object;
	/// Generated member schema for Blueprint and C ABI value copies.
	FString Layout;
	/// Generated SDK struct name for a value payload.
	FName BlueprintType;
	/// Generated read-only view identity for a UObject payload.
	FName TypeId;
	/// Calling-table signature of that view.
	FString Signature;

	bool IsStruct() const
	{
		return !Struct.IsNull();
	}
	bool IsObject() const
	{
		return !Object.IsNull();
	}
	MODAPI_API bool Matches(const FPayload& Value, const UWorld* World) const;
	/// Checks the generated SDK value fields against the host contract.
	MODAPI_API bool MatchesBlueprint(const UScriptStruct& Type) const;
	/// Resolves and validates the generated type before dispatch begins.
	MODAPI_API bool Prepare();

private:
	FString BlueprintLayout;
	TStrongObjectPtr<UScriptStruct> StructType;
	TStrongObjectPtr<UClass> ObjectType;
};

/// Describes the supported reflected fields of a host value struct.
MODAPI_API FString DescribePayloadStruct(const UScriptStruct& Struct);
} // namespace Mod::Extension
