// Copyright Epic Games, Inc. All Rights Reserved.

#include "TP_FirstPersonCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
const FName FirstPersonMeshTag(TEXT("FirstPersonViewMesh"));
const FName FirstPersonCameraTag(TEXT("FirstPersonViewCamera"));
} // namespace

ATP_FirstPersonCharacter::ATP_FirstPersonCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

	// Create the first person mesh that will be viewed only by this character's owner
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));

	FirstPersonMesh->SetupAttachment(GetMesh());

	// Create the Camera Component
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	ConfigureFirstPersonView(*FirstPersonMesh, *FirstPersonCameraComponent);

	// configure the character comps
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// Configure character movement
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;
	GetCharacterMovement()->AirControl = 0.5f;
}

void ATP_FirstPersonCharacter::ConfigureFirstPersonView(USkeletalMeshComponent& Mesh, UCameraComponent& Camera)
{
	Mesh.ComponentTags.AddUnique(FirstPersonMeshTag);
	Mesh.SetOnlyOwnerSee(true);
	Mesh.SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	Mesh.SetCollisionProfileName(FName("NoCollision"));

	Camera.ComponentTags.AddUnique(FirstPersonCameraTag);
	Camera.SetupAttachment(&Mesh, FName("head"));
	Camera.SetRelativeLocationAndRotation(FVector(-2.8f, 5.89f, 0.0f), FRotator(0.0f, 90.0f, -90.0f));
	Camera.bUsePawnControlRotation = true;
	Camera.bEnableFirstPersonFieldOfView = true;
	Camera.bEnableFirstPersonScale = true;
	Camera.FirstPersonFieldOfView = 70.0f;
	Camera.FirstPersonScale = 0.6f;
}

USkeletalMeshComponent* ATP_FirstPersonCharacter::FindFirstPersonMesh(const APawn* Pawn)
{
	return Pawn ? Pawn->FindComponentByTag<USkeletalMeshComponent>(FirstPersonMeshTag) : nullptr;
}

UCameraComponent* ATP_FirstPersonCharacter::FindFirstPersonCamera(const APawn* Pawn)
{
	return Pawn ? Pawn->FindComponentByTag<UCameraComponent>(FirstPersonCameraTag) : nullptr;
}

void ATP_FirstPersonCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Pressed, this, &ATP_FirstPersonCharacter::DoJumpStart);
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Released, this, &ATP_FirstPersonCharacter::DoJumpEnd);
	PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &ATP_FirstPersonCharacter::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &ATP_FirstPersonCharacter::MoveRight);
	PlayerInputComponent->BindAxis(TEXT("Turn"), this, &ATP_FirstPersonCharacter::Turn);
	PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &ATP_FirstPersonCharacter::LookUp);
}

void ATP_FirstPersonCharacter::MoveForward(float Value)
{
	DoMove(0.f, Value);
}

void ATP_FirstPersonCharacter::MoveRight(float Value)
{
	DoMove(Value, 0.f);
}

void ATP_FirstPersonCharacter::Turn(float Value)
{
	DoAim(Value, 0.f);
}

void ATP_FirstPersonCharacter::LookUp(float Value)
{
	DoAim(0.f, Value);
}

void ATP_FirstPersonCharacter::DoAim(float Yaw, float Pitch)
{
	if (GetController())
	{
		// pass the rotation inputs
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void ATP_FirstPersonCharacter::DoMove(float Right, float Forward)
{
	if (GetController())
	{
		// pass the move inputs
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void ATP_FirstPersonCharacter::DoJumpStart()
{
	// pass Jump to the character
	Jump();
}

void ATP_FirstPersonCharacter::DoJumpEnd()
{
	// pass StopJumping to the character
	StopJumping();
}
