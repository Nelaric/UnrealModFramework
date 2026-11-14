// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "AIController.h"
#include "Blueprint/UserWidget.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SaveGame.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Sound/SoundWave.h"
#include "UObject/StrongObjectPtr.h"
#include "WorldCollision.h"

namespace Benchmark
{
constexpr bool CanCallCpp(int32 Case)
{
	// LoadAssets is absent in UE 5.6. The two primary-asset factories have no exported C++ symbol.
	return Case != 156 && Case != 158 && Case != 159;
}

struct FCallContext
{
	explicit FCallContext(UWorld& InWorld) : World(InWorld)
	{
	}

	bool DispatchCpp(int32 Case, uint64& Elapsed, UObject* WorldContext);

	static constexpr float NavigationRadius = 1000.0f;

	UWorld& World;
	ACharacter* Player = nullptr;
	APlayerController* Controller = nullptr;
	ACharacter* SparePawn = nullptr;
	ACharacter* NavigationPawn = nullptr;
	AAIController* AI = nullptr;
	AActor* Actor = nullptr;
	AActor* Other = nullptr;
	USceneComponent* Component = nullptr;
	USceneComponent* Parent = nullptr;
	UStaticMeshComponent* Primitive = nullptr;
	UStaticMeshComponent* OverlapTarget = nullptr;
	UStaticMeshComponent* Physics = nullptr;
	AActor* SampleActor = nullptr;
	USkeletalMeshComponent* Mesh = nullptr;
	UAnimInstance* Anim = nullptr;
	TStrongObjectPtr<UAnimMontage> Montage;
	TStrongObjectPtr<UAnimSequence> Sequence;
	TStrongObjectPtr<UUserWidget> Widget;
	TStrongObjectPtr<UTextBlock> Text;
	TStrongObjectPtr<UImage> Image;
	TStrongObjectPtr<UProgressBar> Progress;
	TStrongObjectPtr<USaveGame> Save;
	TStrongObjectPtr<UTexture2D> Texture;
	UAudioComponent* Audio = nullptr;
	USoundWave* Sound = nullptr;
	UMaterialInterface* Material = nullptr;
	TStrongObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;
	TStrongObjectPtr<UNiagaraSystem> Niagara;

	const FVector ActorStart{0, 300, 120};
	const FVector OtherStart{200, 300, 120};
	const FVector Start{350, 0, 120};
	const FVector End{850, 0, 120};
	const FVector Position{30, 25, 135};
	FVector NavStart{0, 800, 0};
	FVector NavEnd{400, 800, 0};
	const FRotator Rotation{5, 20, 10};
	const FTransform Transform{Rotation, Position, FVector(1.25)};
	const FName Section{TEXT("Benchmark")};
	const FString SlotName{TEXT("BenchmarkSave")};
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	TArray<AActor*> IgnoreActors;
	FHitResult CollisionUV;
	FHitResult Hit;
	FTimerHandle Timer;
	FTraceHandle Trace;
	FTraceDelegate TraceCallback;
	FOverlapDelegate OverlapCallback;
	FLatentActionInfo Latent;
	FTimerDynamicDelegate TimerCallback;
	UKismetSystemLibrary::FOnAssetLoaded AssetCallback;
	UKismetSystemLibrary::FOnAssetClassLoaded ClassCallback;
	TArray<FHitResult> Hits;
	TArray<AActor*> Actors;
	TArray<UPrimitiveComponent*> Components;
	FVector OtherVector = FVector::ZeroVector;
	FRotator RotationResult = FRotator::ZeroRotator;
	FTransform TransformResult = FTransform::Identity;
	int32 CountResult = 0;
	TWeakObjectPtr<UObject> Result;
	FVector VectorResult = FVector::ZeroVector;
	FVector2D ScreenResult = FVector2D::ZeroVector;
	float FloatResult = 0;
	float SecondFloat = 0;
	bool bResult = false;
};
} // namespace Benchmark
