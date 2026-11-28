// Copyright (c) 2026 Nelaric Contributors
#include "TestModShooterComponent.h"

#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/PackageName.h"
#include "ShooterBulletCounterUI.h"
#include "ShooterPickup.h"
#include "ShooterProjectile.h"
#include "ShooterUI.h"
#include "ShooterWeapon.h"
#include "TP_FirstPersonCharacter.h"

DEFINE_LOG_CATEGORY_STATIC(LogTestModShooter, Log, All);

namespace
{
constexpr float AimDistance = 10000.0f;
constexpr float PlayerMaxHealth = 500.0f;
const FName WeaponSocket(TEXT("HandGrip_R"));
} // namespace

void UTestModShooterComponent::GrantTo(APlayerController& PlayerController)
{
	Controller = &PlayerController;
	const FString Root = FPackageName::MountPointExists(TEXT("/TestMod/")) ? TEXT("/TestMod/") : TEXT("/Game/");
	const FString Shooter = Root + TEXT("Variant_Shooter/");
	const FString PickupsPath = Shooter + TEXT("Blueprints/Pickups/");
	auto* Manny = LoadObject<USkeletalMesh>(
	    nullptr, *(Root + TEXT("Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple")));
	auto* PickupClass =
	    LoadClass<AShooterPickup>(nullptr, *(PickupsPath + TEXT("BP_ShooterPickup.BP_ShooterPickup_C")));
	auto* WeaponData = LoadObject<UDataTable>(nullptr, *(PickupsPath + TEXT("DT_WeaponData.DT_WeaponData")));
	auto* BulletCounterClass = LoadClass<UShooterBulletCounterUI>(
	    nullptr, *(Shooter + TEXT("UI/UI_ShooterBulletCounter.UI_ShooterBulletCounter_C")));
	auto* HUDClass = LoadClass<UShooterUI>(nullptr, *(Shooter + TEXT("UI/UI_Shooter.UI_Shooter_C")));
	auto* ExplosionClass = LoadClass<AActor>(
	    nullptr, *(PickupsPath + TEXT("Projectiles/BP_ShooterGrenadeExplosion.BP_ShooterGrenadeExplosion_C")));
	if (!PlayerController.IsLocalController() || !Manny || !PickupClass || !WeaponData || !BulletCounterClass ||
	    !HUDClass || !ExplosionClass)
	{
		UE_LOG(LogTestModShooter, Error, TEXT("Could not initialize the first-person sample with assets from %s."),
		       *Root);
		return;
	}

	auto& Character = *CastChecked<ACharacter>(GetOwner());
	auto& CharacterMesh = *Character.GetMesh();
	auto& Movement = *Character.GetCharacterMovement();
	OriginalPrimitiveType = CharacterMesh.FirstPersonPrimitiveType;
	OriginalAnimTickOption = CharacterMesh.VisibilityBasedAnimTickOption;
	bOriginalOwnerNoSee = CharacterMesh.bOwnerNoSee;
	bOriginalUseControllerRotationYaw = Character.bUseControllerRotationYaw;
	bOriginalOrientRotationToMovement = Movement.bOrientRotationToMovement;
	OriginalAirControl = Movement.AirControl;
	OriginalBrakingDecelerationFalling = Movement.BrakingDecelerationFalling;
	CharacterMesh.SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	CharacterMesh.SetOwnerNoSee(true);
	CharacterMesh.VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Character.bUseControllerRotationYaw = true;
	Movement.bOrientRotationToMovement = false;
	Movement.AirControl = 0.5f;
	Movement.BrakingDecelerationFalling = 1500.0f;

	const TInlineComponentArray<UCameraComponent*> Cameras(&Character);
	for (UCameraComponent* Existing : Cameras)
	{
		if (Existing->IsActive())
		{
			PreviousCameras.Add(Existing);
			Existing->Deactivate();
		}
	}

	FirstPersonMesh = NewObject<USkeletalMeshComponent>(&Character, NAME_None, RF_Transient);
	FirstPersonMesh->SetupAttachment(&CharacterMesh);
	FirstPersonMesh->SetSkeletalMeshAsset(Manny);
	if (CharacterMesh.GetSkeletalMeshAsset())
	{
		FirstPersonMesh->SetLeaderPoseComponent(&CharacterMesh);
	}
	Camera = NewObject<UCameraComponent>(&Character, NAME_None, RF_Transient);
	ATP_FirstPersonCharacter::ConfigureFirstPersonView(*FirstPersonMesh, *Camera);
	FirstPersonMesh->RegisterComponent();
	Camera->RegisterComponent();
	Camera->Activate(true);

	BindWeaponInput(PlayerController);

	BulletCounter = CreateWidget<UShooterBulletCounterUI>(&PlayerController, BulletCounterClass);
	if (BulletCounter)
	{
		BulletCounter->AddToPlayerScreen();
		BulletCounter->BP_UpdateBulletCounter(0, 0);
	}
	ShooterHUD = CreateWidget<UShooterUI>(&PlayerController, HUDClass);
	if (ShooterHUD)
	{
		ShooterHUD->AddToPlayerScreen();
	}
	Character.OnTakeAnyDamage.AddDynamic(this, &UTestModShooterComponent::OnPlayerDamage);
	GrenadeExplosionClass = ExplosionClass;
	ShotActorSpawnedHandle = GetWorld()->AddOnActorSpawnedHandler(
	    FOnActorSpawned::FDelegate::CreateUObject(this, &UTestModShooterComponent::OnShotActorSpawned));
	SpawnPracticeActors(PickupClass, *WeaponData, *Manny);
	UE_LOG(LogTestModShooter, Display, TEXT("Granted template shooter to pawn=%s map=%s."), *Character.GetName(),
	       *GetWorld()->GetMapName());
}

APlayerController* UTestModShooterComponent::GetPlayerController() const
{
	return Controller.Get();
}

void UTestModShooterComponent::BindWeaponInput(APlayerController& PlayerController)
{
	Input = NewObject<UInputComponent>(&PlayerController, NAME_None, RF_Transient);
	Input->RegisterComponent();
	for (const FKey& Key : {EKeys::LeftMouseButton, EKeys::Gamepad_RightTrigger})
	{
		Input->BindKey(Key, IE_Pressed, this, &UTestModShooterComponent::Fire);
	}
	for (const FKey& Key : {EKeys::E, EKeys::Gamepad_RightShoulder, EKeys::Gamepad_FaceButton_Top})
	{
		Input->BindKey(Key, IE_Pressed, this, &UTestModShooterComponent::SwitchWeapon);
	}
	PlayerController.PushInputComponent(Input);
}

void UTestModShooterComponent::SpawnPracticeActors(TSubclassOf<AShooterPickup> PickupClass, UDataTable& WeaponData,
                                                   USkeletalMesh& Manny)
{
	auto& Character = *CastChecked<ACharacter>(GetOwner());
	const FRotator Heading(0.0, Controller->GetControlRotation().Yaw, 0.0);
	const FVector Forward = Heading.Vector();
	const FVector Right = FRotationMatrix(Heading).GetUnitAxis(EAxis::Y);
	const double CapsuleHeight = Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Feet = Character.GetActorLocation() - FVector::UpVector * CapsuleHeight;

	const FName Rows[] = {FName(TEXT("Pistol")), FName(TEXT("Rifle")), FName(TEXT("GrenadeLauncher"))};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Rows); ++Index)
	{
		FDataTableRowHandle WeaponType;
		WeaponType.DataTable = &WeaponData;
		WeaponType.RowName = Rows[Index];
		if (!WeaponType.GetRow<FWeaponTableRow>(TEXT("TestMod weapon pickup")))
		{
			continue;
		}
		const FTransform Transform(Heading, Feet + Forward * 200.0 + Right * (Index - 1) * 130.0);
		auto* Pickup = GetWorld()->SpawnActorDeferred<AShooterPickup>(PickupClass, Transform, &Character, nullptr,
		                                                              ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Pickup)
		{
			Pickup->SetWeaponType(WeaponType);
			Pickup->FinishSpawning(Transform);
			Pickups.Add(Pickup);
		}
	}

	FActorSpawnParameters Parameters;
	Parameters.Owner = &Character;
	Parameters.ObjectFlags = RF_Transient;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Target = GetWorld()->SpawnActor<ACharacter>(Feet + Forward * 570.0 + FVector::UpVector * 100.0,
	                                            (-Forward).Rotation(), Parameters);
	if (Target)
	{
		Target->GetCharacterMovement()->DisableMovement();
		Target->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Target->GetMesh()->SetSkeletalMeshAsset(&Manny);
		Target->GetMesh()->SetRelativeLocationAndRotation(FVector(0.0, 0.0, -88.0), FRotator(0.0, -90.0, 0.0));
		Target->OnTakeAnyDamage.AddDynamic(this, &UTestModShooterComponent::OnTargetDamage);
	}
}

void UTestModShooterComponent::AttachWeaponMeshes(AShooterWeapon* Weapon)
{
	const FAttachmentTransformRules Rules(EAttachmentRule::SnapToTarget, false);
	Weapon->AttachToActor(GetOwner(), Rules);
	Weapon->GetFirstPersonMesh()->AttachToComponent(FirstPersonMesh, Rules, WeaponSocket);
	Weapon->GetThirdPersonMesh()->AttachToComponent(CastChecked<ACharacter>(GetOwner())->GetMesh(), Rules,
	                                                WeaponSocket);
}

void UTestModShooterComponent::PlayFiringMontage(UAnimMontage* Montage)
{
	if (UAnimInstance* Anim = FirstPersonMesh->GetAnimInstance(); Anim && Montage)
	{
		Anim->Montage_Play(Montage);
	}
}

void UTestModShooterComponent::AddWeaponRecoil(float Recoil)
{
	CastChecked<ACharacter>(GetOwner())->AddControllerPitchInput(Recoil);
}

void UTestModShooterComponent::UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize)
{
	if (BulletCounter)
	{
		BulletCounter->BP_UpdateBulletCounter(MagazineSize, CurrentAmmo);
	}
}

FVector UTestModShooterComponent::GetWeaponTargetLocation()
{
	const FVector Start = Camera->GetComponentLocation();
	const FVector End = Start + Controller->GetControlRotation().Vector() * AimDistance;
	FHitResult Hit;
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(TestModAim), false, GetOwner());
	return GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Query) ? Hit.ImpactPoint : End;
}

void UTestModShooterComponent::AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass)
{
	if (!WeaponClass || Weapons.ContainsByPredicate([&WeaponClass](const AShooterWeapon* Weapon)
	                                                { return IsValid(Weapon) && Weapon->IsA(WeaponClass); }))
	{
		return;
	}

	auto& Character = *CastChecked<ACharacter>(GetOwner());
	FActorSpawnParameters Parameters;
	Parameters.Owner = &Character;
	Parameters.Instigator = &Character;
	Parameters.ObjectFlags = RF_Transient;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (auto* Weapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, Character.GetActorTransform(), Parameters))
	{
		Weapons.Add(Weapon);
		if (IsValid(CurrentWeapon))
		{
			CurrentWeapon->DeactivateWeapon();
		}
		Weapon->ActivateWeapon();
	}
}

void UTestModShooterComponent::OnWeaponActivated(AShooterWeapon* Weapon)
{
	CurrentWeapon = Weapon;
	if (const auto& Anim = Weapon->GetFirstPersonAnimInstanceClass())
	{
		FirstPersonMesh->SetAnimInstanceClass(Anim);
	}
	UpdateWeaponHUD(Weapon->GetBulletCount(), Weapon->GetMagazineSize());
	UE_LOG(LogTestModShooter, Display, TEXT("Equipped template weapon=%s ammo=%d."), *Weapon->GetClass()->GetName(),
	       Weapon->GetBulletCount());
}

void UTestModShooterComponent::OnWeaponDeactivated(AShooterWeapon* Weapon)
{
	if (CurrentWeapon == Weapon)
	{
		CurrentWeapon = nullptr;
	}
}

void UTestModShooterComponent::SwitchWeapon()
{
	if (IsValid(CurrentWeapon) && Controller.IsValid() && Controller->GetPawn() == GetOwner() && Weapons.Num() > 1)
	{
		const int32 Next = (Weapons.Find(CurrentWeapon) + 1) % Weapons.Num();
		CurrentWeapon->DeactivateWeapon();
		Weapons[Next]->ActivateWeapon();
	}
}

void UTestModShooterComponent::Fire()
{
	if (IsValid(CurrentWeapon) && Controller.IsValid() && Controller->GetPawn() == GetOwner() &&
	    CurrentWeapon->StartFiring())
	{
		UE_LOG(LogTestModShooter, Display, TEXT("Fired one projectile, weapon=%s ammo=%d."),
		       *CurrentWeapon->GetClass()->GetName(), CurrentWeapon->GetBulletCount());
	}
}

void UTestModShooterComponent::OnShotActorSpawned(AActor* Actor)
{
	if ((Actor->IsA<AShooterProjectile>() && Actor->GetInstigator() == GetOwner()) ||
	    Actor->GetClass() == GrenadeExplosionClass)
	{
		ShotActors.RemoveAll([](const TWeakObjectPtr<AActor>& Item) { return !Item.IsValid(); });
		ShotActors.Add(Actor);
	}
}

void UTestModShooterComponent::OnTargetDamage(AActor* DamagedActor, float Damage, const UDamageType*, AController*,
                                              AActor* DamageCauser)
{
	if (DamagedActor == Target && DamageCauser && DamageCauser->GetInstigator() == GetOwner() && Damage > 0.0f)
	{
		TargetHealth = FMath::Max(0.0f, TargetHealth - Damage);
		UE_LOG(LogTestModShooter, Display, TEXT("Practice target hit, health=%.1f."), TargetHealth);
		if (TargetHealth == 0.0f)
		{
			Target->Destroy();
			Target = nullptr;
		}
	}
}

void UTestModShooterComponent::OnPlayerDamage(AActor*, float Damage, const UDamageType*, AController*, AActor*)
{
	if (BulletCounter && Damage > 0.0f)
	{
		PlayerHealth = FMath::Max(0.0f, PlayerHealth - Damage);
		BulletCounter->BP_Damaged(PlayerHealth / PlayerMaxHealth);
	}
}

void UTestModShooterComponent::EndPlay(EEndPlayReason::Type Reason)
{
	if (IsValid(Input))
	{
		if (APlayerController* PlayerController = Controller.Get())
		{
			PlayerController->PopInputComponent(Input);
		}
		Input->DestroyComponent();
		Input = nullptr;
	}
	if (BulletCounter)
	{
		BulletCounter->RemoveFromParent();
		BulletCounter = nullptr;
	}
	if (ShooterHUD)
	{
		ShooterHUD->RemoveFromParent();
		ShooterHUD = nullptr;
	}
	if (ShotActorSpawnedHandle.IsValid())
	{
		GetWorld()->RemoveOnActorSpawnedHandler(ShotActorSpawnedHandle);
	}
	GrenadeExplosionClass = nullptr;
	for (const auto& ShotActor : ShotActors)
	{
		if (AActor* Actor = ShotActor.Get())
		{
			Actor->Destroy();
		}
	}
	for (AShooterWeapon* Weapon : Weapons)
	{
		if (IsValid(Weapon))
		{
			Weapon->Destroy();
		}
	}
	for (AShooterPickup* Pickup : Pickups)
	{
		if (IsValid(Pickup))
		{
			Pickup->Destroy();
		}
	}
	CurrentWeapon = nullptr;
	Weapons.Reset();
	Pickups.Reset();
	if (IsValid(Target))
	{
		Target->Destroy();
		Target = nullptr;
	}
	if (Camera)
	{
		Camera->DestroyComponent();
		FirstPersonMesh->DestroyComponent();
		Camera = nullptr;
		FirstPersonMesh = nullptr;
		for (const auto& Previous : PreviousCameras)
		{
			if (UCameraComponent* Existing = Previous.Get())
			{
				Existing->Activate(true);
			}
		}
		if (auto* Character = Cast<ACharacter>(GetOwner()))
		{
			Character->OnTakeAnyDamage.RemoveDynamic(this, &UTestModShooterComponent::OnPlayerDamage);
			Character->GetMesh()->SetFirstPersonPrimitiveType(OriginalPrimitiveType);
			Character->GetMesh()->SetOwnerNoSee(bOriginalOwnerNoSee);
			Character->GetMesh()->VisibilityBasedAnimTickOption = OriginalAnimTickOption;
			Character->bUseControllerRotationYaw = bOriginalUseControllerRotationYaw;
			Character->GetCharacterMovement()->bOrientRotationToMovement = bOriginalOrientRotationToMovement;
			Character->GetCharacterMovement()->AirControl = OriginalAirControl;
			Character->GetCharacterMovement()->BrakingDecelerationFalling = OriginalBrakingDecelerationFalling;
		}
		UE_LOG(LogTestModShooter, Display, TEXT("Revoked template shooter from pawn=%s."), *GetNameSafe(GetOwner()));
	}
	Super::EndPlay(Reason);
}
