// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "ShooterWeaponHolder.h"
#include "TestModShooterComponent.generated.h"

class ACharacter;
class AController;
class APlayerController;
class AShooterPickup;
class UCameraComponent;
class UDamageType;
class UDataTable;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class UShooterBulletCounterUI;
class UShooterUI;
class USkeletalMesh;
class USkeletalMeshComponent;

UCLASS(MinimalAPI)
class UTestModShooterComponent : public UActorComponent, public IShooterWeaponHolder
{
	GENERATED_BODY()

public:
	void GrantTo(APlayerController& PlayerController);
	APlayerController* GetPlayerController() const;

protected:
	void EndPlay(EEndPlayReason::Type Reason) override;

private:
	void AttachWeaponMeshes(AShooterWeapon* Weapon) override;
	void PlayFiringMontage(UAnimMontage* Montage) override;
	void AddWeaponRecoil(float Recoil) override;
	void UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize) override;
	FVector GetWeaponTargetLocation() override;
	void AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass) override;
	void OnWeaponActivated(AShooterWeapon* Weapon) override;
	void OnWeaponDeactivated(AShooterWeapon* Weapon) override;

	void Fire();
	void SwitchWeapon();
	void BindWeaponInput(APlayerController& PlayerController, UEnhancedInputLocalPlayerSubsystem& Subsystem,
	                     const UInputMappingContext& MappingContext, const UInputAction& FireAction,
	                     const UInputAction& SwapAction);
	void SpawnPracticeActors(TSubclassOf<AShooterPickup> PickupClass, UDataTable& WeaponData, USkeletalMesh& Manny);
	void OnShotActorSpawned(AActor* Actor);

	UFUNCTION()
	void OnTargetDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType,
	                    AController* InstigatorController, AActor* DamageCauser);

	UFUNCTION()
	void OnPlayerDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType,
	                    AController* InstigatorController, AActor* DamageCauser);

	TWeakObjectPtr<APlayerController> Controller;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
	TArray<TWeakObjectPtr<UCameraComponent>> PreviousCameras;
	TArray<TWeakObjectPtr<AActor>> ShotActors;
	FDelegateHandle ShotActorSpawnedHandle;

	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

	UPROPERTY()
	TObjectPtr<UEnhancedInputComponent> Input;

	UPROPERTY()
	TObjectPtr<UInputMappingContext> Mapping;

	UPROPERTY()
	TObjectPtr<UShooterBulletCounterUI> BulletCounter;

	UPROPERTY()
	TObjectPtr<UShooterUI> ShooterHUD;

	UPROPERTY()
	TArray<TObjectPtr<AShooterWeapon>> Weapons;

	UPROPERTY()
	TObjectPtr<AShooterWeapon> CurrentWeapon;

	UPROPERTY()
	TArray<TObjectPtr<AShooterPickup>> Pickups;

	UPROPERTY()
	TObjectPtr<ACharacter> Target;

	UPROPERTY()
	TSubclassOf<AActor> GrenadeExplosionClass;

	EFirstPersonPrimitiveType OriginalPrimitiveType = EFirstPersonPrimitiveType::None;
	EVisibilityBasedAnimTickOption OriginalAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
	float OriginalAirControl = 0.0f;
	float OriginalBrakingDecelerationFalling = 0.0f;
	float TargetHealth = 100.0f;
	float PlayerHealth = 500.0f;
	bool bOriginalOwnerNoSee = false;
	bool bOriginalOrientRotationToMovement = false;
	bool bOriginalUseControllerRotationYaw = false;
};
