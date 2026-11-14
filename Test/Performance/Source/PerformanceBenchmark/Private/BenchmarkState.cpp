// Copyright (c) 2026 Nelaric Contributors
#include "BenchmarkSession.h"

#include "Animation/AnimInstance.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/LevelStreaming.h"
#include "GameFramework/AsyncActionHandleSaveGame.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NiagaraFunctionLibrary.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

namespace Benchmark
{
bool FSession::SetUp(int32 Case)
{
	Result.Reset();
	bResult = false;
	CountResult = 0;
	FloatResult = 0;
	Hit = FHitResult{};
	Hits.Reset();
	Actors.Reset();
	Components.Reset();
	if (!IsCpp(ActivePath))
	{
		BlueprintSample() = {};
	}
	CallbackTarget->bDone = false;
	CallbackTarget->bSuccess = true;
	Proxy.Reset();
	Trace = FTraceHandle();
	Latent = FLatentActionInfo(0, Case, TEXT("Complete"), CallbackTarget.Get());
	auto* Move = Player->GetCharacterMovement();
	if (Case >= 8 && Case <= 13)
	{
		Actor->SetActorTransform(FTransform(ActorStart));
	}
	if (Case >= 24 && Case <= 30)
	{
		Component->SetWorldTransform(FTransform(ActorStart));
	}
	if (Case >= 37 && Case <= 56)
	{
		Move->SetGravityDirection(FVector(0, 0, -1));
		Move->SetMovementMode(MOVE_Walking);
		Move->Velocity = FVector(10, 0, 0);
	}
	if (Case >= 93 && Case <= 110)
	{
		Anim->Montage_Stop(0.0f, Montage.Get());
		if (Case >= 94 && Case <= 108 && Case != 95)
		{
			if (Anim->Montage_Play(Montage.Get()) <= 0)
			{
				Fail(TEXT("Montage fixture could not play"));
				return false;
			}
			if (Case == 98)
			{
				Anim->Montage_Pause(Montage.Get());
			}
		}
	}
	if (Case >= 167 && Case <= 176)
	{
		Physics->SetSimulatePhysics(Case != 167);
		Physics->SetEnableGravity(false);
		Physics->SetWorldLocationAndRotation(FVector(1800, 0, 300), FRotator::ZeroRotator, false, nullptr,
		                                     ETeleportType::ResetPhysics);
		if (Case != 167)
		{
			Physics->SetPhysicsLinearVelocity(FVector(10, 0, 0));
			Physics->SetPhysicsAngularVelocityInDegrees(FVector(0, 10, 0));
			Physics->WakeAllRigidBodies();
		}
	}
	if (Case >= 177 && Case <= 186)
	{
		AI->StopMovement();
		NavigationPawn->SetActorLocation(NavStart + FVector(0, 0, 100), false, nullptr, ETeleportType::TeleportPhysics);
	}
	switch (Case)
	{
	case 7:
		Actor->SetOwner(nullptr);
		break;
	case 14:
		Actor->SetActorHiddenInGame(false);
		break;
	case 15:
		Actor->SetActorEnableCollision(true);
		break;
	case 17:
		SampleActor = World.SpawnActor<AActor>();
		break;
	case 20:
		SampleActor = UGameplayStatics::BeginDeferredActorSpawnFromClass(
		    Driver, AActor::StaticClass(), Transform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		break;
	case 31:
		Component->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		break;
	case 32:
		Component->AttachToComponent(Parent, FAttachmentTransformRules::KeepWorldTransform);
		break;
	case 34:
		Component->SetComponentTickEnabled(false);
		break;
	case 35:
		Component->Deactivate();
		break;
	case 36:
		Component->Activate(true);
		break;
	case 37:
		Player->StopJumping();
		break;
	case 38:
		Player->Jump();
		break;
	case 40:
		Move->bWantsToCrouch = false;
		break;
	case 41:
		Move->bWantsToCrouch = true;
		break;
	case 46:
		Player->JumpMaxHoldTime = 0.25f;
		Player->Jump();
		Player->CheckJumpInput(0.01f);
		break;
	case 50:
		Move->SetMovementMode(MOVE_Falling);
		break;
	case 52:
		Move->SetMovementMode(MOVE_Swimming);
		break;
	case 53:
		Move->SetMovementMode(MOVE_Flying);
		break;
	case 61:
	case 62:
		Player->AddMovementInput(FVector::ForwardVector, 0.5f, true);
		break;
	case 63:
		Player->AddMovementInput(FVector::ForwardVector, 0.5f, true);
		Player->ConsumeMovementInputVector();
		break;
	case 69:
	case 70:
		if (Controller->GetPawn() != Player)
		{
			Controller->Possess(Player);
		}
		break;
	case 76:
	case 77:
	case 78:
	case 79:
	case 80:
	case 81:
	case 83:
		Controller->SetControlRotation(FRotator::ZeroRotator);
		Controller->SetMouseLocation(320, 240);
		break;
	case 82:
		Controller->SetViewTarget(Player);
		break;
	case 84:
	case 85:
	case 86:
	case 87:
		if (Controller->PlayerInput)
		{
			auto* Input = Controller->PlayerInput.Get();
			Input->FlushPressedKeys();
			Input->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, EKeys::W, IE_Pressed));
			Input->ProcessInputStack({}, 0.016f, false);
			if (Case == 86)
			{
				Input->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, EKeys::W, IE_Released));
				Input->ProcessInputStack({}, 0.016f, false);
			}
		}
		break;
	case 91:
		Controller->ResetIgnoreMoveInput();
		break;
	case 92:
		Controller->ResetIgnoreLookInput();
		break;
	case 109:
		Anim->AddCurveValue(TEXT("BenchmarkCurve"), 0.75f);
		break;
	case 111:
	case 112:
		Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Mesh->SetAnimation(nullptr);
		break;
	case 137:
	case 138:
	case 139:
	case 140:
		Primitive->UpdateOverlaps();
		break;
	case 142:
		Primitive->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		break;
	case 143:
		Primitive->SetCollisionObjectType(ECC_WorldDynamic);
		break;
	case 144:
		Primitive->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		break;
	case 145:
		Primitive->SetGenerateOverlapEvents(true);
		break;
	case 148:
		if (IsCpp(ActivePath))
		{
			UKismetSystemLibrary::RetriggerableDelay(&Receiver(), 30.0f, Latent);
		}
		break;
	case 149:
		Component->SetRelativeLocation(FVector::ZeroVector);
		break;
	case 151:
		if (IsCpp(ActivePath))
		{
			TimerCallback.BindUFunction(&Receiver(), TEXT("TimerCallback"));
		}
		break;
	case 152:
	case 153:
	case 154:
		TimerCallback.BindUFunction(&Receiver(), TEXT("TimerCallback"));
		World.GetTimerManager().SetTimer(Timer, TimerCallback, 60.0f, false);
		if (Case == 154)
		{
			World.GetTimerManager().PauseTimer(Timer);
		}
		break;
	case 158:
	case 159:
		UAssetManager::Get().ScanPathsForPrimaryAssets(Primary.PrimaryAssetType, {TEXT("/Game/Fixtures")},
		                                               UBenchmarkAsset::StaticClass(), false);
		break;
	case 160:
	case 161:
	{
		const bool bLoad = Case == 161;
		if (Streaming->IsLevelLoaded() != bLoad || Streaming->IsLevelVisible() != bLoad)
		{
			Streaming->SetShouldBeLoaded(bLoad);
			Streaming->SetShouldBeVisible(bLoad);
			World.FlushLevelStreaming(EFlushLevelStreamingType::Full);
		}
		break;
	}
	case 187:
		Widget->RemoveFromParent();
		break;
	case 188:
	case 193:
	case 194:
		Widget->AddToViewport(2);
		break;
	case 189:
		Widget->SetVisibility(ESlateVisibility::Visible);
		break;
	case 190:
		Text->SetText(FText::GetEmpty());
		break;
	case 191:
		Image->SetBrushFromTexture(nullptr, false);
		break;
	case 192:
		Progress->SetPercent(0.25f);
		break;
	case 196:
		Primitive->SetMaterial(0, Material);
		break;
	case 197:
		DynamicMaterial->SetScalarParameterValue(TEXT("Roughness"), 0.25f);
		break;
	case 199:
		Audio->Stop();
		break;
	default:
		break;
	}
	return true;
}

bool FSession::IsPending(int32 Case) const
{
	const bool bCpp = IsCpp(ActivePath);
	if (Case == 147 || Case == 148 || Case == 149 || Case == 155 || Case == 157 || (Case >= 158 && Case <= 163))
	{
		return bCpp ? !CallbackTarget->bDone : BlueprintSample().Completed == 0;
	}
	if (Case >= 164 && Case <= 166)
	{
		return !CallbackTarget->bDone;
	}
	return false;
}

bool FSession::Validate(int32 Case) const
{
	const bool bCpp = IsCpp(ActivePath);
	if (Case >= 147 && Case <= 163 && bCpp && !CallbackTarget->bSuccess)
	{
		return false;
	}
	switch (Case)
	{
	case 7:
		return Actor->GetOwner() == Other;
	case 8:
		return Actor->GetActorLocation().Equals(Position);
	case 9:
		return Actor->GetActorRotation().Equals(Rotation);
	case 10:
		return Actor->GetActorTransform().Equals(Transform);
	case 14:
		return Actor->IsHidden();
	case 15:
		return !Actor->GetActorEnableCollision();
	case 17:
		return !IsValid(SampleActor) || SampleActor->IsActorBeingDestroyed();
	case 19:
		return IsValid(ObservedObject());
	case 20:
		return SampleActor && SampleActor->HasActorBegunPlay();
	case 31:
		return Component->GetAttachParent() == Parent;
	case 32:
		return Component->GetAttachParent() == nullptr;
	case 34:
		return Component->IsComponentTickEnabled();
	case 35:
		return Component->IsActive();
	case 36:
		return !Component->IsActive();
	case 48:
		return Player->GetCharacterMovement()->MovementMode == MOVE_None;
	case 68:
		return SparePawn->GetController() != nullptr;
	case 69:
		return Controller->GetPawn() == SparePawn;
	case 70:
		return Controller->GetPawn() == nullptr;
	case 91:
		return Controller->IsMoveInputIgnored();
	case 92:
		return Controller->IsLookInputIgnored();
	case 93:
	case 95:
		return Anim->Montage_IsPlaying(Montage.Get());
	case 142:
		return Primitive->GetCollisionEnabled() == ECollisionEnabled::QueryOnly;
	case 143:
		return Primitive->GetCollisionObjectType() == ECC_PhysicsBody;
	case 144:
		return Primitive->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Overlap;
	case 145:
		return !Primitive->GetGenerateOverlapEvents();
	case 146:
		return !bCpp || bResult;
	case 152:
		return !World.GetTimerManager().TimerExists(Timer);
	case 153:
		return World.GetTimerManager().IsTimerPaused(Timer);
	case 154:
		return World.GetTimerManager().IsTimerActive(Timer);
	case 160:
		return Streaming->IsLevelLoaded() && Streaming->IsLevelVisible();
	case 161:
		return !Streaming->IsLevelLoaded();
	case 164:
	case 165:
	case 166:
		return CallbackTarget->bSuccess;
	case 167:
		return Physics->IsSimulatingPhysics();
	case 176:
		return Physics->IsGravityEnabled();
	case 181:
	case 182:
		// Recast has a bounded search for a random point. Both outcomes are valid samples.
		return true;
	case 183:
		return !bCpp || bResult;
	case 184:
		return !bCpp || (Result.IsValid() && CastChecked<UNavigationPath>(Result.Get())->IsValid());
	case 186:
		return !bCpp || CountResult == EPathFollowingRequestResult::RequestSuccessful;
	case 187:
		return Widget->IsInViewport();
	case 188:
		return !Widget->IsInViewport();
	case 189:
		return Widget->GetVisibility() == ESlateVisibility::Collapsed;
	case 192:
		return FMath::IsNearlyEqual(Progress->GetPercent(), 0.75f);
	case 196:
		return Cast<UMaterialInstanceDynamic>(Primitive->GetMaterial(0)) != nullptr;
	case 200:
		return IsValid(ObservedObject());
	default:
		if (Case >= 115 && Case <= 136 && bCpp)
		{
			return bResult;
		}
		return true;
	}
}

bool FSession::TearDown(int32 Case)
{
	if (!Validate(Case))
	{
		Fail(TEXT("The UE result did not match the benchmark fixture"));
		return false;
	}
	switch (Case)
	{
	case 17:
		SampleActor = nullptr;
		break;
	case 19:
		if (auto* Spawned = Cast<AActor>(ObservedObject()))
		{
			Spawned->Destroy();
		}
		break;
	case 20:
		SampleActor->Destroy();
		SampleActor = nullptr;
		break;
	case 31:
		Component->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		break;
	case 42:
		Player->GetCharacterMovement()->PendingLaunchVelocity = FVector::ZeroVector;
		break;
	case 46:
		Player->StopJumping();
		Player->JumpMaxHoldTime = 0.0f;
		break;
	case 68:
		if (auto* SpawnedController = SparePawn->GetController())
		{
			SpawnedController->UnPossess();
			SpawnedController->Destroy();
		}
		break;
	case 69:
	case 70:
		Controller->Possess(Player);
		break;
	case 111:
	case 112:
		Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
		Anim = Mesh->GetAnimInstance();
		break;
	case 142:
		Primitive->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		break;
	case 143:
		Primitive->SetCollisionObjectType(ECC_WorldDynamic);
		break;
	case 144:
		Primitive->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		break;
	case 145:
		Primitive->SetGenerateOverlapEvents(true);
		break;
	case 150:
	case 151:
		World.GetTimerManager().ClearAllTimersForObject(&Receiver());
		break;
	case 152:
	case 153:
	case 154:
		World.GetTimerManager().ClearTimer(Timer);
		break;
	case 187:
	case 193:
	case 194:
		Widget->RemoveFromParent();
		Controller->SetInputMode(FInputModeGameOnly());
		break;
	case 199:
		Audio->Stop();
		break;
	case 200:
		if (auto* Effect = Cast<UNiagaraComponent>(ObservedObject()))
		{
			Effect->DestroyComponent();
		}
		break;
	default:
		break;
	}
	return true;
}
} // namespace Benchmark
