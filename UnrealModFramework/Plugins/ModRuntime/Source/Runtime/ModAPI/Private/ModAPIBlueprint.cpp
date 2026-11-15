// Copyright (c) 2026 Nelaric Contributors
#include "ModAPIBlueprint.h"

#include "Algo/Compare.h"
#include "CoreGlobals.h"
#include "Misc/ScopeExit.h"
#include "ModExtensionPayload.h"
#include "ModExtensionLibrary.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace Mod::API
{
namespace
{
uint64 BlueprintGeneration = 1;

bool IsRebuildingBlueprints()
{
#if WITH_EDITOR
	return GCompilingBlueprint || GIsReinstancing.load();
#else
	return false;
#endif
}

bool IsSupportedScalar(const FProperty& Property)
{
	if (const auto* Byte = CastField<FByteProperty>(&Property))
	{
		return Byte->Enum == nullptr;
	}
	return Property.IsA<FBoolProperty>() || Property.IsA<FIntProperty>() || Property.IsA<FInt64Property>() ||
	       Property.IsA<FFloatProperty>() || Property.IsA<FDoubleProperty>();
}
} // namespace

TModResult<TArray<FBlueprintParameter>> FBlueprintCall::Describe(const UFunction& Function, bool bProjected)
{
	using FResult = TModResult<TArray<FBlueprintParameter>>;
	if (!IsInGameThread())
	{
		return FResult::Failure(EError::WrongThread);
	}
	if (Function.HasAnyFunctionFlags(FUNC_Native | FUNC_Static | FUNC_Net | FUNC_Delegate | FUNC_MulticastDelegate |
	                                 FUNC_UbergraphFunction | FUNC_EditorOnly | FUNC_BlueprintAuthorityOnly |
	                                 FUNC_BlueprintCosmetic) ||
	    Function.Script.IsEmpty())
	{
		return FResult::Failure(EError::SignatureMismatch);
	}
	TArray<FBlueprintParameter> Parameters;
	bool bHasResult = false;
	for (TFieldIterator<FProperty> It(&Function, EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		const FProperty& Property = **It;
		if (!Property.HasAnyPropertyFlags(CPF_Parm))
		{
			continue;
		}
		const bool bReturn = Property.HasAnyPropertyFlags(CPF_ReturnParm);
		const bool bOutput =
		    bReturn || (Property.HasAnyPropertyFlags(CPF_OutParm) && !Property.HasAnyPropertyFlags(CPF_ConstParm));
		FName TypePath;
		if (bProjected)
		{
			if (const auto* Struct = CastField<FStructProperty>(&Property);
			    Struct && (Struct->Struct == FModPayloadObject::StaticStruct() ||
			               !Mod::Extension::DescribePayloadStruct(*Struct->Struct).IsEmpty()))
			{
				TypePath = FName(Struct->Struct->GetPathName());
			}
			else if (const auto* Object = CastField<FObjectProperty>(&Property);
			         Object && !Object->IsA<FClassProperty>())
			{
				TypePath = FName(Object->PropertyClass->GetPathName());
			}
		}
		const bool bValue =
		    (IsSupportedScalar(Property) && Property.HasAnyPropertyFlags(CPF_NoDestructor)) || !TypePath.IsNone();
		const bool bReference = Property.HasAnyPropertyFlags(CPF_ReferenceParm) &&
		                        (TypePath.IsNone() || !Property.HasAnyPropertyFlags(CPF_ConstParm));
		if (!bValue || Property.ArrayDim != 1 || bReference || bHasResult || Property.GetOffset_ForUFunction() < 0 ||
		    !Property.IsInContainer(Function.ParmsSize))
		{
			return FResult::Failure(EError::SignatureMismatch);
		}
		bHasResult |= bOutput;
		Parameters.Add({Property.GetFName(), Property.GetClass()->GetFName(),
		                bReturn   ? EBlueprintParameter::Return
		                : bOutput ? EBlueprintParameter::Output
		                          : EBlueprintParameter::Input,
		                TypePath});
	}
	if (Parameters.Num() != Function.NumParms)
	{
		return FResult::Failure(EError::SignatureMismatch);
	}
	return FResult::Success(MoveTemp(Parameters));
}

TModResult<TSharedPtr<const FBlueprintCall>> FBlueprintCall::Bind(UClass& DeclaringClass, UClass& ActualClass,
                                                                  const FBlueprintFunction& Expected, bool bProjected)
{
	using FResult = TModResult<TSharedPtr<const FBlueprintCall>>;
	if (!IsInGameThread())
	{
		return FResult::Failure(EError::WrongThread);
	}
	if (IsRebuildingBlueprints() || ActualClass.HasAnyClassFlags(CLASS_NewerVersionExists))
	{
		return FResult::Failure(EError::BindingExpired);
	}
	UFunction* Declared = DeclaringClass.FindFunctionByName(Expected.Name, EIncludeSuperFlag::ExcludeSuper);
	UFunction* Actual = ActualClass.FindFunctionByName(Expected.Name);
	if (!Declared || !Actual || !ActualClass.IsChildOf(&DeclaringClass))
	{
		return FResult::Failure(EError::SignatureMismatch);
	}
	const UFunction* Parent = Actual;
	while (Parent && Parent != Declared)
	{
		Parent = Parent->GetSuperFunction();
	}
	if (!Parent)
	{
		return FResult::Failure(EError::SignatureMismatch);
	}
	const auto Shape = Describe(*Actual, bProjected);
	const auto DeclaredShape = Actual == Declared ? Shape : Describe(*Declared, bProjected);
	if (!Shape.IsSuccess() || !DeclaredShape.IsSuccess() || !Algo::Compare(Shape.GetValue(), Expected.Parameters) ||
	    !Algo::Compare(DeclaredShape.GetValue(), Expected.Parameters))
	{
		return FResult::Failure(EError::SignatureMismatch);
	}
	auto Bound = MakeShared<FBlueprintCall>();
	Bound->Function = Actual;
	Bound->Generation = BlueprintGeneration;
	for (TFieldIterator<FProperty> It(Actual, EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		if (It->HasAnyPropertyFlags(CPF_Parm))
		{
			Bound->Properties.Add(*It);
		}
	}
	return FResult::Success(MoveTemp(Bound));
}

void FBlueprintCall::Invalidate()
{
	check(IsInGameThread());
	++BlueprintGeneration;
}

bool FBlueprintCall::IsCurrent() const
{
	return Generation == BlueprintGeneration && Function.IsValid() && !IsRebuildingBlueprints();
}

EError FBlueprintCall::Process(UObject& Target, TFunctionRef<void(void*)> Write,
                               TFunctionRef<void(const void*)> Read) const
{
	UFunction* Resolved = Function.Get();
	if (!Resolved || Generation != BlueprintGeneration || IsRebuildingBlueprints())
	{
		return EError::BindingExpired;
	}
	void* Parameters = FMemory_Alloca_Aligned(Resolved->ParmsSize, Resolved->GetMinAlignment());
	if (Parameters)
	{
		FMemory::Memzero(Parameters, Resolved->ParmsSize);
		for (const FProperty* Property : Properties)
		{
			Property->InitializeValue_InContainer(Parameters);
		}
	}
	ON_SCOPE_EXIT
	{
		for (const FProperty* Property : Properties)
		{
			if (!Property->HasAnyPropertyFlags(CPF_NoDestructor))
			{
				Property->DestroyValue_InContainer(Parameters);
			}
		}
	};
	Write(Parameters);
	Target.ProcessEvent(Resolved, Parameters);
	if (!IsCurrent())
	{
		return EError::BindingExpired;
	}
	Read(Parameters);
	return EError::None;
}
} // namespace Mod::API
