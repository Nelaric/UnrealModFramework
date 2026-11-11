// Copyright Epic Games, Inc. All Rights Reserved.

#include "TP_FirstPersonCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TP_FirstPerson.h"

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
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this,
		                                   &ATP_FirstPersonCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this,
		                                   &ATP_FirstPersonCharacter::DoJumpEnd);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this,
		                                   &ATP_FirstPersonCharacter::MoveInput);

		// Looking/Aiming
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this,
		                                   &ATP_FirstPersonCharacter::LookInput);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this,
		                                   &ATP_FirstPersonCharacter::LookInput);
	}
	else
	{
		UE_LOG(LogTP_FirstPerson, Error,
		       TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input "
		            "system. If you intend to use the legacy system, then you will need to update this C++ file."),
		       *GetNameSafe(this));
	}
}

void ATP_FirstPersonCharacter::MoveInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	FVector2D MovementVector = Value.Get<FVector2D>();

	// pass the axis values to the move input
	DoMove(MovementVector.X, MovementVector.Y);
}

void ATP_FirstPersonCharacter::LookInput(const FInputActionValue& Value)
{
	// get the Vector2D look axis
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// pass the axis values to the aim input
	DoAim(LookAxisVector.X, LookAxisVector.Y);
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
