// Copyright (c) 2026 Nelaric Contributors
#include "BenchmarkSession.h"

#include "Animation/AnimInstance.h"
#include "Components/InputComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/Texture2D.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "ModInstance.h"
#include "NiagaraFunctionLibrary.h"
#include "UObject/UnrealType.h"

namespace Benchmark
{
namespace
{
template <class T> bool Struct(FProperty& Property, void* Value, const T& Argument)
{
	auto* Field = CastField<FStructProperty>(&Property);
	if (!Field)
	{
		return false;
	}
	*static_cast<T*>(Value) = Argument;
	return true;
}

template <class T> void Array(FArrayProperty& Property, void* Value, const TArray<T>& Source)
{
	FScriptArrayHelper Helper(&Property, Value);
	Helper.Resize(Source.Num());
	for (int32 Index = 0; Index < Source.Num(); ++Index)
	{
		*reinterpret_cast<T*>(Helper.GetRawPtr(Index)) = Source[Index];
	}
}

bool IsSubmissionEvent(int32 Id)
{
	return Id == 147 || Id == 148 || Id == 149 || Id == 160 || Id == 161;
}
} // namespace

UObject& FSession::Receiver() const
{
	return ActivePath == EPath::ModCpp || ActivePath == EPath::ModBlueprint ? static_cast<UObject&>(*Mod.Get())
	                                                                        : static_cast<UObject&>(*Driver);
}

FBlueprintSample& FSession::BlueprintSample() const
{
	return ActivePath == EPath::ModBlueprint ? *ModSample : Driver->Sample;
}

UObject* FSession::ObservedObject() const
{
	return IsCpp(ActivePath) ? Result.Get() : BlueprintSample().Object.Get();
}

bool FSession::DispatchModCpp(int32 Case, uint64& Elapsed)
{
	struct
	{
		int32 Case;
		int64 ContextAddress;
		int64 ReturnValue = -1;
	} Parameters{Case, static_cast<int64>(reinterpret_cast<UPTRINT>(static_cast<FCallContext*>(this)))};
	Mod->ProcessEvent(ModNativeCall, &Parameters);
	if (Parameters.ReturnValue < 0)
	{
		return false;
	}
	Elapsed = static_cast<uint64>(Parameters.ReturnValue);
	return true;
}

bool FSession::DispatchBlueprint(int32 Case, uint64& Elapsed)
{
	UObject& Target = Receiver();
	const FName Entry(*FString::Printf(TEXT("Blueprint%03d"), Case));
	UFunction* Function = Target.FindFunction(Entry);
	if (!Function)
	{
		Fail(TEXT("Missing Blueprint entry"));
		return false;
	}
	FStructOnScope Params(Function);
	if (!SetArguments(*Function, Params.GetStructMemory(), Case))
	{
		return false;
	}
	if (Case == 148)
	{
		// Re-enter the same node before it can tick, preserving the generated latent UUID.
		Target.ProcessEvent(Function, Params.GetStructMemory());
	}
	const uint64 StartCycles = FPlatformTime::Cycles64();
	Target.ProcessEvent(Function, Params.GetStructMemory());
	const uint64 EventCycles = FPlatformTime::Cycles64() - StartCycles;
	const FBlueprintSample& Sample = BlueprintSample();
	if (Sample.Started == 0)
	{
		Fail(TEXT("Blueprint did not execute its sampling graph"));
		return false;
	}
	Elapsed = IsSubmissionEvent(Case) ? EventCycles : Sample.Cycles;
	return true;
}

bool FSession::SetArguments(UFunction& Function, uint8* Buffer, int32 Case)
{
	const FRotator Zero = FRotator::ZeroRotator;
	const FVector Motion(20, 5, 25);
	const FVector CubePos = Primitive->GetComponentLocation();
	const auto TraceType = UEngineTypes::ConvertToTraceType(ECC_Visibility);
	for (TFieldIterator<FProperty> It(&Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		FProperty& Property = **It;
		if (Property.HasAnyPropertyFlags(CPF_ReturnParm))
		{
			continue;
		}
		void* Value = Property.ContainerPtrToValuePtr<void>(Buffer);
		const FString Name = Property.GetName();

		if (auto* ClassField = CastField<FClassProperty>(&Property))
		{
			UClass* Class = nullptr;
			if (Name == TEXT("ActorClass"))
			{
				Class = Case == 18 ? Player->GetClass() : AActor::StaticClass();
			}
			else if (Name == TEXT("ComponentClass"))
			{
				Class = USceneComponent::StaticClass();
			}
			ClassField->SetObjectPropertyValue(Value, Class);
		}
		else if (auto* SoftField = CastField<FSoftObjectProperty>(&Property))
		{
			const TCHAR* Path = Case == 155 ? TEXT("/Game/Fixtures/BenchmarkAsset.BenchmarkAsset")
			                                : TEXT("/Game/Fixtures/BP_BenchmarkWidget.BP_BenchmarkWidget_C");
			SoftField->SetPropertyValue(Value, FSoftObjectPtr(FSoftObjectPath(Path)));
		}
		else if (auto* ObjectField = CastField<FObjectPropertyBase>(&Property))
		{
			UObject* Object = nullptr;
			if (Name == TEXT("Receiver"))
			{
				if (Case <= 17)
					Object = Case == 17 ? SampleActor : Actor;
				else if (Case <= 36)
					Object = Component;
				else if (Case <= 46 || (Case >= 57 && Case <= 68))
					Object = Case == 68 ? SparePawn : Player;
				else if (Case <= 56)
					Object = Player->GetCharacterMovement();
				else if (Case <= 92)
					Object = Controller;
				else if (Case <= 94)
					Object = Player;
				else if (Case <= 109)
					Object = Anim;
				else if (Case <= 114)
					Object = Mesh;
				else if (Case >= 137 && Case <= 145)
					Object = Primitive;
				else if (Case >= 167 && Case <= 176)
					Object = Physics;
				else if (Case == 186)
					Object = AI;
				else if (Case == 190)
					Object = Text.Get();
				else if (Case == 191)
					Object = Image.Get();
				else if (Case == 192)
					Object = Progress.Get();
				else if (Case >= 187 && Case <= 193)
					Object = Widget.Get();
				else if (Case == 195 || Case == 196)
					Object = Primitive;
				else if (Case == 197)
					Object = DynamicMaterial.Get();
				else if (Case == 199)
					Object = Audio;
			}
			else if (Name == TEXT("WorldContextObject") || Name == TEXT("Object"))
				Object = &Receiver();
			else if (Name == TEXT("OtherActor") || Name == TEXT("NewOwner") || Name == TEXT("NewViewTarget") ||
			         Name == TEXT("Goal") || (Name == TEXT("Other") && Case != 139 && Case != 140))
				Object = Other;
			else if (Name == TEXT("SampleActor") || Name == TEXT("Actor"))
				Object = SampleActor;
			else if (Name == TEXT("Parent"))
				Object = Parent;
			else if (Name == TEXT("InPawn"))
				Object = SparePawn;
			else if (Name == TEXT("Pawn") || Name == TEXT("ControlledActor") || Name == TEXT("PathfindingContext"))
				Object = NavigationPawn;
			else if (Name == TEXT("Other") || Name == TEXT("OtherComp"))
			{
				Object = Case == 139 ? static_cast<UObject*>(OverlapTarget->GetOwner()) : OverlapTarget;
			}
			else if (Name == TEXT("AnimMontage") || Name == TEXT("MontageToPlay") || Name == TEXT("Montage"))
				Object = Montage.Get();
			else if (Name == TEXT("NewAnimToPlay"))
				Object = Sequence.Get();
			else if (Name == TEXT("Texture"))
				Object = Texture.Get();
			else if (Name == TEXT("Component"))
				Object = Component;
			else if (Name == TEXT("SaveGameObject"))
				Object = Save.Get();
			else if (Name == TEXT("Controller"))
				Object = AI;
			else if (Name == TEXT("PlayerController"))
				Object = Controller;
			else if (Name == TEXT("InWidgetToFocus"))
				Object = Widget.Get();
			else if (Name == TEXT("SourceMaterial"))
				Object = Material;
			else if (Name == TEXT("Sound"))
				Object = Sound;
			else if (Name == TEXT("SystemTemplate"))
				Object = Niagara.Get();
			if (Object && !Object->IsA(ObjectField->PropertyClass))
			{
				Fail(FString::Printf(TEXT("Unexpected object class for pin %s"), *Name));
				return false;
			}
			ObjectField->SetObjectPropertyValue(Value, Object);
		}
		else if (auto* BoolField = CastField<FBoolProperty>(&Property))
		{
			const bool Setting =
			    (Name == TEXT("bSweep") &&
			     (Case == 8 || Case == 10 || Case == 11 || Case == 12 || Case == 24 || Case == 26)) ||
			    Name == TEXT("bNewHidden") || Name == TEXT("bEnabled") || Name == TEXT("bNewMoveInput") ||
			    Name == TEXT("bNewLookInput") || Name == TEXT("bXYOverride") || Name == TEXT("bZOverride") ||
			    Name == TEXT("bForce") || Name == TEXT("bStopAllMontages") || Name == TEXT("bRemoveZeroWeight") ||
			    Name == TEXT("bMakeVisibleAfterLoad") || Name == TEXT("bSimulate") || Name == TEXT("bGravityEnabled") ||
			    Name == TEXT("bAccelChange") || Name == TEXT("bVelChange") || Name == TEXT("bUsePathfinding") ||
			    Name == TEXT("bCanStrafe") || Name == TEXT("bMatchSize") || Name == TEXT("bAutoActivate") ||
			    (Name == TEXT("bLooping") && Case == 111);
			BoolField->SetPropertyValue(Value, Setting);
		}
		else if (auto* NumericField = CastField<FNumericProperty>(&Property);
		         NumericField && NumericField->IsFloatingPoint())
		{
			double Number = 0;
			if (Name == TEXT("Val") || Name == TEXT("ScaleValue"))
				Number = 0.5;
			else if (Name == TEXT("ScreenX"))
				Number = 320;
			else if (Name == TEXT("ScreenY"))
				Number = 240;
			else if (Name == TEXT("BlendTime"))
				Number = 0.25;
			else if (Name == TEXT("InPlayRate") || Name == TEXT("PitchMultiplier"))
				Number = 1;
			else if (Name == TEXT("NewPlayRate"))
				Number = 1.5;
			else if (Name == TEXT("NewPosition"))
				Number = 0.1;
			else if (Name == TEXT("Value") || Name == TEXT("InPercent"))
				Number = 0.75;
			else if (Name == TEXT("Radius"))
				Number = Case >= 181 ? NavigationRadius : Case >= 131 ? 90 : 12;
			else if (Name == TEXT("SphereRadius"))
				Number = 90;
			else if (Name == TEXT("HalfHeight"))
				Number = Case >= 131 ? 120 : 24;
			else if (Name == TEXT("Duration") || Name == TEXT("OverTime"))
				Number = 0.001;
			else if (Name == TEXT("Time"))
				Number = 60;
			else if (Name == TEXT("AcceptanceRadius"))
				Number = 5;
			else if (Name == TEXT("VolumeMultiplier"))
				Number = 0.005;
			else if (Name == TEXT("DrawTime"))
				Number = 5;
			NumericField->SetFloatingPointPropertyValue(Value, Number);
		}
		else if (CastField<FNumericProperty>(&Property) || CastField<FEnumProperty>(&Property))
		{
			int64 Number = 0;
			if (Name == TEXT("X"))
				Number = 320;
			else if (Name == TEXT("Y"))
				Number = 240;
			else if (Name == TEXT("ZOrder"))
				Number = 2;
			else if (Name == TEXT("CollisionHandlingOverride"))
				Number = static_cast<int64>(ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			else if (Name == TEXT("TransformScaleMethod"))
				Number = static_cast<int64>(ESpawnActorScaleMethod::MultiplyWithRoot);
			else if (Name.EndsWith(TEXT("Rule")))
				Number = Case == 32 ? static_cast<int64>(EDetachmentRule::KeepWorld)
				                    : static_cast<int64>(EAttachmentRule::KeepWorld);
			else if (Name == TEXT("NewMovementMode"))
				Number = MOVE_Flying;
			else if (Name == TEXT("TraceChannel"))
				Number = TraceType;
			else if (Name == TEXT("WhichStick"))
				Number = EControllerAnalogStick::CAS_LeftStick;
			else if (Name == TEXT("NewType"))
				Number = static_cast<int64>(ECollisionEnabled::QueryOnly);
			else if (Name == TEXT("Channel"))
				Number = Case == 143 ? ECC_PhysicsBody : ECC_Visibility;
			else if (Name == TEXT("NewResponse"))
				Number = ECR_Overlap;
			else if (Name == TEXT("InVisibility"))
				Number = static_cast<int64>(ESlateVisibility::Collapsed);
			else if (Name == TEXT("TransformSpace"))
				Number = RTS_World;
			else if (Name == TEXT("InMouseLockMode"))
				Number = static_cast<int64>(EMouseLockMode::DoNotLock);
			if (auto* Enum = CastField<FEnumProperty>(&Property))
			{
				Enum->GetUnderlyingProperty()->SetIntPropertyValue(Value, Number);
			}
			else
			{
				CastFieldChecked<FNumericProperty>(&Property)->SetIntPropertyValue(Value, Number);
			}
		}
		else if (auto* NameField = CastField<FNameProperty>(&Property))
		{
			FName TextValue = NAME_None;
			if (Name == TEXT("Tag"))
				TextValue = TEXT("Benchmark");
			else if (Name == TEXT("LevelName"))
				TextValue = TEXT("BenchmarkSublevel");
			else if (Name == TEXT("CurveName"))
				TextValue = TEXT("BenchmarkCurve");
			else if (Name == TEXT("MorphTargetName"))
				TextValue = TEXT("BenchmarkMorph");
			else if (Name == TEXT("InBoneName"))
				TextValue = TEXT("pelvis");
			else if (Name == TEXT("SectionName") || Name == TEXT("NextSection"))
				TextValue = Section;
			else if (Name == TEXT("SectionNameToChange"))
				TextValue = TEXT("Default");
			else if (Name == TEXT("ParameterName"))
				TextValue = TEXT("Roughness");
			NameField->SetPropertyValue(Value, TextValue);
		}
		else if (auto* StringField = CastField<FStrProperty>(&Property))
		{
			StringField->SetPropertyValue(Value, Name == TEXT("SlotName") ? SlotName : TEXT("TimerCallback"));
		}
		else if (auto* TextField = CastField<FTextProperty>(&Property))
		{
			TextField->SetPropertyValue(Value, FText::FromString(TEXT("Benchmark")));
		}
		else if (auto* ArrayField = CastField<FArrayProperty>(&Property))
		{
			if (Name == TEXT("ActorsToIgnore"))
				Array(*ArrayField, Value, IgnoreActors);
			else if (Name == TEXT("ObjectTypes"))
				Array(*ArrayField, Value, ObjectTypes);
			else if (Name == TEXT("PrimaryAssetList"))
				Array(*ArrayField, Value, TArray<FPrimaryAssetId>{Primary});
		}
		else if (auto* StructField = CastField<FStructProperty>(&Property))
		{
			const FName StructName = StructField->Struct->GetFName();
			if (StructName == TEXT("Vector"))
			{
				FVector V = FVector::ZeroVector;
				if (Name == TEXT("Start"))
					V = Start;
				else if (Name == TEXT("End"))
					V = End;
				else if (Name == TEXT("DeltaLocation") || Name == TEXT("LaunchVelocity") || Name == TEXT("Force") ||
				         Name == TEXT("Impulse") || Name == TEXT("Torque") || Name == TEXT("NewVel") ||
				         Name == TEXT("NewAngVel"))
					V = Motion;
				else if (Name == TEXT("NewLocation") || Name == TEXT("TargetRelativeLocation"))
					V = Position;
				else if (Name == TEXT("NewScale3D"))
					V = FVector(1.25);
				else if (Name == TEXT("WorldLocation"))
					V = FVector(600, 0, 120);
				else if (Name == TEXT("WorldDirection"))
					V = FVector::ForwardVector;
				else if (Name == TEXT("HalfSize"))
					V = FVector(12);
				else if (Name == TEXT("Extent") || Name == TEXT("BoxExtent"))
					V = FVector(90);
				else if (Name.EndsWith(TEXT("Pos")))
					V = CubePos;
				else if (Name == TEXT("Point"))
					V = Case == 141 ? Start : NavStart;
				else if (Name == TEXT("GravityDir"))
					V = FVector(0, -0.6, -0.8);
				else if (Name == TEXT("Destination") || Name == TEXT("Goal") || Name == TEXT("PathEnd") ||
				         Name == TEXT("RayEnd"))
					V = NavEnd;
				else if (Name == TEXT("Origin") || Name == TEXT("PathStart") || Name == TEXT("RayStart"))
					V = NavStart;
				else if (Name == TEXT("QueryExtent"))
					V = FVector(100, 100, 300);
				else if (Name == TEXT("Location"))
					V = ActorStart;
				else if (Name == TEXT("Scale"))
					V = FVector::OneVector;
				Struct(Property, Value, V);
			}
			else if (StructName == TEXT("Rotator"))
			{
				Struct(Property, Value,
				       Case == 9 || Case == 25 || Case == 28 || Case == 71 || Case == 149 ? Rotation : Zero);
			}
			else if (StructName == TEXT("Transform"))
				Struct(Property, Value, Transform);
			else if (StructName == TEXT("Vector2D"))
				Struct(Property, Value, FVector2D(30, 25));
			else if (StructName == TEXT("HitResult"))
				Struct(Property, Value, CollisionUV);
			else if (StructName == TEXT("LinearColor"))
				Struct(Property, Value, Name == TEXT("TraceColor") ? FLinearColor::Red : FLinearColor::Green);
			else if (StructName == TEXT("Key"))
				Struct(Property, Value, EKeys::W);
			else if (StructName == TEXT("TimerHandle"))
				Struct(Property, Value, Timer);
			else if (StructName == TEXT("PrimaryAssetId"))
				Struct(Property, Value, Primary);
			else
			{
				Fail(FString::Printf(TEXT("Unsupported struct pin %s (%s)"), *Name, *StructName.ToString()));
				return false;
			}
		}
		else
		{
			Fail(FString::Printf(TEXT("Unsupported parameter %s"), *Name));
			return false;
		}
	}
	return true;
}
} // namespace Benchmark
