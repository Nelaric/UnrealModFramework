// Copyright (c) 2026 Nelaric Contributors
#include "TestModPreview.h"

#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/LatentActionManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "TestModInstance.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogTestModPreview, Log, All);

ATestModPreviewPawn::ATestModPreviewPawn()
{
	GetMeshComponent()->SetVisibility(false);

	PreviewMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PreviewSkeletalMesh"));
	PreviewMesh->SetupAttachment(GetRootComponent());
	PreviewMesh->SetRelativeLocation(FVector(0.0, 0.0, -88.0));
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Quinn(
	    TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
	PreviewMesh->SetSkeletalMeshAsset(Quinn.Object);

	UCameraComponent* Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("PreviewCamera"));
	Camera->SetupAttachment(GetRootComponent());
	const FVector CameraOffset(-320.0, -240.0, 130.0);
	Camera->SetRelativeLocationAndRotation(CameraOffset, (-CameraOffset).Rotation());
	Camera->SetFieldOfView(50.0f);

	UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("PreviewLight"));
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetupAttachment(GetRootComponent());
	Light->SetRelativeLocation(FVector(-180.0, -120.0, 200.0));
	Light->SetIntensity(6000.0f);
	Light->SetAttenuationRadius(800.0f);

	static ConstructorHelpers::FClassFinder<UTestModInstance> Blueprint(TEXT("/Game/BP_CallbackProbe"));
	CallbackClass = Blueprint.Class;
}

void ATestModPreviewPawn::PawnClientRestart()
{
	Super::PawnClientRestart();
	if (CallbackInstance || !CallbackClass)
	{
		return;
	}

	CallbackInstance = NewObject<UTestModInstance>(this, CallbackClass);
	FModLoadNotification Result;
	Result.ModId = TEXT("testmod");
	Result.bStartup = true;
	Result.bSucceeded = true;
	CallbackInstance->OnModLoadCompleted(Result);
}

void ATestModPreviewPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld(); World && CallbackInstance)
	{
		World->GetLatentActionManager().RemoveActionsForObject(CallbackInstance.Get());
		CallbackInstance = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

bool ATestModPreviewPawn::SetMesh(USkeletalMesh* NewMesh)
{
	if (!IsValid(NewMesh) || !NewMesh->GetSkeleton())
	{
		return false;
	}

	PreviewMesh->SetSkeletalMeshAsset(NewMesh);
	UE_LOG(LogTestModPreview, Display, TEXT("SetMesh applied %s to %s."),
	       *GetPathNameSafe(PreviewMesh->GetSkeletalMeshAsset()), *GetName());
	return true;
}

ATestModGameMode::ATestModGameMode()
{
	DefaultPawnClass = ATestModPreviewPawn::StaticClass();
}
