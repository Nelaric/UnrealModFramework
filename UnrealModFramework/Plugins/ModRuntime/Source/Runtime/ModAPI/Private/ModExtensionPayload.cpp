// Copyright (c) 2026 Nelaric Contributors
#include "ModExtensionPayload.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace Mod::Extension
{
namespace
{
enum class EFieldNames
{
	Host,
	Ignore,
	SDK
};

bool Describe(const UScriptStruct& Struct, FString& Out, int32 Depth, int32& Count,
              EFieldNames Names = EFieldNames::Host)
{
	if (Depth == 8 || Struct.GetStructureSize() > 4096 || Struct.GetSuperStruct())
	{
		return false;
	}
	Out += TEXT("{");
	int32 Index = 0;
	for (TFieldIterator<FProperty> It(&Struct, EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		const auto& Field = **It;
		if (++Count > 128 || Field.ArrayDim != 1 || Field.HasAnyPropertyFlags(CPF_EditorOnly | CPF_Deprecated))
		{
			return false;
		}
		const FString Name = Field.GetName();
		if (Names == EFieldNames::SDK && Name != FString::Printf(TEXT("Field%d"), Index++))
		{
			return false;
		}
		Out += (Names == EFieldNames::Host ? *Name : TEXT("_"));
		Out += TEXT(":");
		if (const auto* Nested = CastField<FStructProperty>(&Field))
		{
			if (!Describe(*Nested->Struct, Out, Depth + 1, Count, Names))
			{
				return false;
			}
		}
		else if (Field.IsA<FFloatProperty>() || Field.IsA<FDoubleProperty>() || Field.IsA<FBoolProperty>() ||
		         Field.IsA<FIntProperty>() || Field.IsA<FInt64Property>() ||
		         (Field.IsA<FByteProperty>() && !CastFieldChecked<FByteProperty>(&Field)->Enum))
		{
			Out += Field.GetClass()->GetName();
		}
		else
		{
			return false;
		}
		Out += TEXT(";");
	}
	Out += TEXT("}");
	return true;
}
} // namespace

FPayload FPayload::FromStruct(const UScriptStruct& Type, const void* Value)
{
	FPayload Payload;
	Payload.Struct.InitializeAs(&Type, static_cast<const uint8*>(Value));
	return Payload;
}

FPayload FPayload::FromObject(const UObject* Value)
{
	FPayload Payload;
	Payload.Object = Value;
	return Payload;
}

bool FPayloadType::Prepare()
{
	if (IsStruct())
	{
		auto* Type = Cast<UScriptStruct>(Struct.TryLoad());
		if (!Type || Layout.IsEmpty() || Layout != DescribePayloadStruct(*Type) || BlueprintType.IsNone())
		{
			return false;
		}
		BlueprintLayout.Reset();
		int32 Fields = 0;
		if (!Describe(*Type, BlueprintLayout, 0, Fields, EFieldNames::Ignore))
		{
			return false;
		}
		StructType.Reset(Type);
	}
	else if (IsObject())
	{
		auto* Type = Object.TryLoadClass<UObject>();
		if (!Type || TypeId.IsNone() || Signature.IsEmpty())
		{
			return false;
		}
		ObjectType.Reset(Type);
	}
	return true;
}

bool FPayloadType::Matches(const FPayload& Value, const UWorld* World) const
{
	if (IsStruct())
	{
		return Value.GetStruct().IsValid() &&
		       Value.GetStruct().GetScriptStruct() == (StructType ? StructType.Get() : Struct.ResolveObject());
	}
	const auto* Class = ObjectType ? ObjectType.Get() : Object.ResolveClass();
	if (!Class || Value.GetStruct().IsValid())
	{
		return false;
	}
	const auto* Actual = Value.GetObject();
	if (!Actual)
	{
		return Value.IsNullObject();
	}
	if (!Actual->IsA(Class) || (Actual->GetWorld() && Actual->GetWorld() != World))
	{
		return false;
	}
	const auto* Actor = Cast<AActor>(Actual);
	const auto* Component = Cast<UActorComponent>(Actual);
	return (!Actor || !Actor->IsActorBeingDestroyed()) && (!Component || !Component->IsBeingDestroyed());
}

bool FPayloadType::MatchesBlueprint(const UScriptStruct& Type) const
{
	FString Shape;
	int32 Fields = 0;
	return Type.GetFName() == BlueprintType && Type.GetOutermost()->HasAnyPackageFlags(PKG_CompiledIn) &&
	       Describe(Type, Shape, 0, Fields, EFieldNames::SDK) && Fields > 0 && Shape == BlueprintLayout;
}

FString DescribePayloadStruct(const UScriptStruct& Struct)
{
	FString Layout;
	int32 Fields = 0;
	return Describe(Struct, Layout, 0, Fields) && Fields > 0 ? Layout : FString();
}
} // namespace Mod::Extension
