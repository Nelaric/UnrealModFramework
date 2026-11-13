// Copyright (c) 2026 Nelaric Contributors
/** @file ModAPIBlueprint.h */
#pragma once

#include "ModAPIResult.h"
#include "Templates/Function.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FProperty;
class UClass;
class UFunction;
class UObject;

namespace Mod::API
{
/// Parameter roles supported by synchronous Blueprint exports.
enum class EBlueprintParameter : uint8
{
	/// A scalar passed by value into the Blueprint.
	Input,
	/// A scalar output written through a Blueprint result pin.
	Output,
	/// The Unreal function return property.
	Return,
};

/// A generated parameter description independent of its compiled byte offset.
struct FBlueprintParameter
{
	/// Property name in the compiled function.
	FName Name;
	/// Exact Unreal property class, such as FloatProperty.
	FName PropertyClass;
	/// Parameter direction and result convention.
	EBlueprintParameter Role;

	bool operator==(const FBlueprintParameter& Other) const = default;
};

/// One Blueprint function and its expected reflected parameter list.
struct FBlueprintFunction
{
	/// The compiled function name in the declaring class.
	FName Name;
	/// Module-owned parameter descriptions in reflection order.
	TConstArrayView<FBlueprintParameter> Parameters;
};

/// Validated function and argument properties for one Blueprint class.
class MODAPI_API FBlueprintCall
{
public:
	/** @brief Describes a synchronous Blueprint function on the game thread.
	 * @param Function The compiled Blueprint function.
	 * @return The ordered parameter descriptions or a signature error.
	 */
	static TModResult<TArray<FBlueprintParameter>> Describe(const UFunction& Function);

public:
	static TModResult<TSharedPtr<const FBlueprintCall>> Bind(UClass& DeclaringClass, UClass& ActualClass,
	                                                         const FBlueprintFunction& Expected);
	static void Invalidate();
	bool IsCurrent() const;
	const FProperty* GetParameter(int32 Index) const
	{
		return Properties[Index];
	}
	EError Process(UObject& Target, TFunctionRef<void(void*)> Write, TFunctionRef<void(const void*)> Read) const;

private:
	TWeakObjectPtr<UFunction> Function;
	TArray<const FProperty*> Properties;
	uint64 Generation = 0;
};
} // namespace Mod::API
