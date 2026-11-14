// Copyright (c) 2026 Nelaric Contributors
#include "BenchmarkCalls.h"

#include "Animation/AnimInstance.h"
#include "Blueprint/AIAsyncTaskBlueprintProxy.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/AsyncActionHandleSaveGame.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PawnMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "TimerManager.h"

namespace Benchmark
{
bool FCallContext::DispatchCpp(int32 Case, uint64& Elapsed, UObject* WorldContext)
{
	using Kismet = UKismetSystemLibrary;
	using Gameplay = UGameplayStatics;
	using Navigation = UNavigationSystemV1;
	using AIHelper = UAIBlueprintHelperLibrary;
	static const FName Tag(TEXT("Benchmark"));
	static const FName Level(TEXT("BenchmarkSublevel"));
	static const FName Bone(TEXT("pelvis"));
	static const FName Parameter(TEXT("Roughness"));
	const auto Channel = UEngineTypes::ConvertToTraceType(ECC_Visibility);
	const auto Draw = EDrawDebugTrace::None;
	const FVector Extent(12, 12, 12);
	const FRotator Zero = FRotator::ZeroRotator;
	const FVector Motion(20, 5, 25);
	auto* Move = Player->GetCharacterMovement();
	auto* SamplePawn = Case == 68 ? SparePawn : Player;
	auto* MontageAsset = Montage.Get();
	auto* TextureAsset = Texture.Get();
	auto* NiagaraAsset = Niagara.Get();
	const FCollisionQueryParams& Query = FCollisionQueryParams::DefaultQueryParam;
	const FCollisionResponseParams& Response = FCollisionResponseParams::DefaultResponseParam;
	const uint64 Started = FPlatformTime::Cycles64();
	switch (Case)
	{
	case 1:
		VectorResult = Actor->K2_GetActorLocation();
		break;
	case 2:
		RotationResult = Actor->K2_GetActorRotation();
		break;
	case 3:
		VectorResult = Actor->GetVelocity();
		break;
	case 4:
		FloatResult = Actor->GetDistanceTo(Other);
		break;
	case 5:
		bResult = Actor->ActorHasTag(Tag);
		break;
	case 6:
		Result = Actor->GetOwner();
		break;
	case 7:
		Actor->SetOwner(Other);
		break;
	case 8:
		bResult = Actor->K2_SetActorLocation(Position, true, Hit, false);
		break;
	case 9:
		bResult = Actor->K2_SetActorRotation(Rotation, false);
		break;
	case 10:
		bResult = Actor->K2_SetActorTransform(Transform, true, Hit, false);
		break;
	case 11:
		Actor->K2_AddActorWorldOffset(Motion, true, Hit, false);
		break;
	case 12:
		Actor->K2_AddActorLocalOffset(Motion, true, Hit, false);
		break;
	case 13:
		Actor->SetActorScale3D(FVector(1.25));
		break;
	case 14:
		Actor->SetActorHiddenInGame(true);
		break;
	case 15:
		Actor->SetActorEnableCollision(false);
		break;
	case 16:
		CountResult = Actor->K2_GetComponentsByClass(USceneComponent::StaticClass()).Num();
		break;
	case 17:
		SampleActor->K2_DestroyActor();
		break;
	case 18:
		Gameplay::GetAllActorsOfClass(WorldContext, Player->GetClass(), Actors);
		break;
	case 19:
		Result = Gameplay::BeginDeferredActorSpawnFromClass(WorldContext, AActor::StaticClass(), Transform,
		                                                    ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		break;
	case 20:
		Result = Gameplay::FinishSpawningActor(SampleActor, Transform);
		break;
	case 21:
		VectorResult = Component->K2_GetComponentLocation();
		break;
	case 22:
		RotationResult = Component->K2_GetComponentRotation();
		break;
	case 23:
		TransformResult = Component->K2_GetComponentToWorld();
		break;
	case 24:
		Component->K2_SetWorldLocation(Position, true, Hit, false);
		break;
	case 25:
		Component->K2_SetWorldRotation(Rotation, false, Hit, false);
		break;
	case 26:
		Component->K2_SetWorldTransform(Transform, true, Hit, false);
		break;
	case 27:
		Component->K2_SetRelativeLocation(Position, false, Hit, false);
		break;
	case 28:
		Component->K2_SetRelativeRotation(Rotation, false, Hit, false);
		break;
	case 29:
		Component->K2_AddWorldOffset(Motion, false, Hit, false);
		break;
	case 30:
		Component->K2_AddLocalOffset(Motion, false, Hit, false);
		break;
	case 31:
		bResult = Component->K2_AttachToComponent(Parent, NAME_None, EAttachmentRule::KeepWorld,
		                                          EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, false);
		break;
	case 32:
		Component->K2_DetachFromComponent(EDetachmentRule::KeepWorld, EDetachmentRule::KeepWorld,
		                                  EDetachmentRule::KeepWorld, false);
		break;
	case 33:
		VectorResult = Component->GetForwardVector();
		break;
	case 34:
		Component->SetComponentTickEnabled(true);
		break;
	case 35:
		Component->Activate(false);
		break;
	case 36:
		Component->Deactivate();
		break;
	case 37:
		Player->Jump();
		break;
	case 38:
		Player->StopJumping();
		break;
	case 39:
		bResult = Player->CanJump();
		break;
	case 40:
		Player->Crouch(false);
		break;
	case 41:
		Player->UnCrouch(false);
		break;
	case 42:
		Player->LaunchCharacter(Motion, true, true);
		break;
	case 43:
		Result = Player->GetCharacterMovement();
		break;
	case 44:
		Result = Player->GetMesh();
		break;
	case 45:
		Result = Player->GetCapsuleComponent();
		break;
	case 46:
		bResult = Player->IsJumpProvidingForce();
		break;
	case 47:
		Move->SetMovementMode(MOVE_Flying, 0);
		break;
	case 48:
		Move->DisableMovement();
		break;
	case 49:
		static_cast<UMovementComponent*>(Move)->StopMovementImmediately();
		break;
	case 50:
		bResult = Move->IsFalling();
		break;
	case 51:
		bResult = Move->IsMovingOnGround();
		break;
	case 52:
		bResult = Move->IsSwimming();
		break;
	case 53:
		bResult = Move->IsFlying();
		break;
	case 54:
		VectorResult = Move->GetCurrentAcceleration();
		break;
	case 55:
		FloatResult = Move->K2_GetWalkableFloorAngle();
		break;
	case 56:
		Move->SetGravityDirection(FVector(0, -0.6, -0.8));
		break;
	case 57:
		Result = Player->GetController();
		break;
	case 58:
		RotationResult = Player->GetControlRotation();
		break;
	case 59:
		Result = Player->GetMovementComponent();
		break;
	case 60:
		Player->AddMovementInput(FVector::ForwardVector, 0.5f, true);
		break;
	case 61:
		VectorResult = Player->ConsumeMovementInputVector();
		break;
	case 62:
		VectorResult = Player->GetPendingMovementInputVector();
		break;
	case 63:
		VectorResult = Player->GetLastMovementInputVector();
		break;
	case 64:
		Player->AddControllerYawInput(0.5f);
		break;
	case 65:
		Player->AddControllerPitchInput(0.5f);
		break;
	case 66:
		bResult = Player->IsPawnControlled();
		break;
	case 67:
		bResult = Player->IsLocallyControlled();
		break;
	case 68:
		SamplePawn->SpawnDefaultController();
		break;
	case 69:
		Controller->Possess(SparePawn);
		break;
	case 70:
		Controller->UnPossess();
		break;
	case 71:
		Controller->SetControlRotation(Rotation);
		break;
	case 72:
		Result = Controller->GetPawn().Get();
		break;
	case 73:
		Controller->GetPlayerViewPoint(VectorResult, RotationResult);
		break;
	case 74:
		Result = Controller->GetViewTarget();
		break;
	case 75:
	{
		int32 X, Y;
		Controller->GetViewportSize(X, Y);
		CountResult = X;
		break;
	}
	case 76:
		bResult = Controller->ProjectWorldLocationToScreen(FVector(600, 0, 120), ScreenResult, false);
		break;
	case 77:
		bResult = Controller->DeprojectScreenPositionToWorld(320.0f, 240.0f, VectorResult, OtherVector);
		break;
	case 78:
		bResult = Controller->DeprojectMousePositionToWorld(VectorResult, OtherVector);
		break;
	case 79:
		bResult = Controller->GetMousePosition(FloatResult, SecondFloat);
		break;
	case 80:
		bResult = Controller->GetHitResultUnderCursorByChannel(Channel, false, Hit);
		break;
	case 81:
		bResult = Controller->GetHitResultUnderCursorForObjects(ObjectTypes, false, Hit);
		break;
	case 82:
		Controller->SetViewTargetWithBlend(Other, 0.25f, VTBlend_Linear, 0, false);
		break;
	case 83:
		Controller->SetMouseLocation(320, 240);
		break;
	case 84:
		bResult = Controller->IsInputKeyDown(EKeys::W);
		break;
	case 85:
		bResult = Controller->WasInputKeyJustPressed(EKeys::W);
		break;
	case 86:
		bResult = Controller->WasInputKeyJustReleased(EKeys::W);
		break;
	case 87:
		FloatResult = Controller->GetInputKeyTimeDown(EKeys::W);
		break;
	case 88:
		Controller->GetInputMouseDelta(FloatResult, SecondFloat);
		break;
	case 89:
		Controller->GetInputAnalogStickState(EControllerAnalogStick::CAS_LeftStick, FloatResult, SecondFloat);
		break;
	case 90:
		Result = Controller->GetHUD();
		break;
	case 91:
		Controller->SetIgnoreMoveInput(true);
		break;
	case 92:
		Controller->SetIgnoreLookInput(true);
		break;
	case 93:
		FloatResult = Player->PlayAnimMontage(MontageAsset, 1.0f, NAME_None);
		break;
	case 94:
		Player->StopAnimMontage(MontageAsset);
		break;
	case 95:
		FloatResult = Anim->Montage_Play(MontageAsset, 1.0f, EMontagePlayReturnType::MontageLength, 0, true);
		break;
	case 96:
		Anim->Montage_Stop(0, MontageAsset);
		break;
	case 97:
		Anim->Montage_Pause(MontageAsset);
		break;
	case 98:
		Anim->Montage_Resume(MontageAsset);
		break;
	case 99:
		Anim->Montage_JumpToSection(Section, MontageAsset);
		break;
	case 100:
		Anim->Montage_SetNextSection(TEXT("Default"), Section, MontageAsset);
		break;
	case 101:
		Anim->Montage_SetPlayRate(MontageAsset, 1.5f);
		break;
	case 102:
		Anim->Montage_SetPosition(MontageAsset, 0.1f);
		break;
	case 103:
		FloatResult = Anim->Montage_GetPosition(MontageAsset);
		break;
	case 104:
		FloatResult = Anim->Montage_GetPlayRate(MontageAsset);
		break;
	case 105:
		bResult = Anim->Montage_IsPlaying(MontageAsset);
		break;
	case 106:
		bResult = Anim->Montage_IsActive(MontageAsset);
		break;
	case 107:
		bResult = Anim->IsAnyMontagePlaying();
		break;
	case 108:
		Result = Anim->GetCurrentActiveMontage();
		break;
	case 109:
		FloatResult = Anim->GetCurveValue(TEXT("BenchmarkCurve"));
		break;
	case 110:
		Result = Mesh->GetAnimInstance();
		break;
	case 111:
		Mesh->PlayAnimation(Sequence.Get(), true);
		break;
	case 112:
		Mesh->SetAnimation(Sequence.Get());
		break;
	case 113:
		Mesh->SetMorphTarget(TEXT("BenchmarkMorph"), 0.75f, true);
		break;
	case 114:
		TransformResult = Mesh->GetBoneTransform(Bone, RTS_World);
		break;
	case 115:
		bResult = Kismet::LineTraceSingle(WorldContext, Start, End, Channel, false, IgnoreActors, Draw, Hit, false);
		break;
	case 116:
		bResult = Kismet::LineTraceMulti(WorldContext, Start, End, Channel, false, IgnoreActors, Draw, Hits, false);
		break;
	case 117:
		bResult = Kismet::LineTraceSingleForObjects(WorldContext, Start, End, ObjectTypes, false, IgnoreActors, Draw,
		                                            Hit, false);
		break;
	case 118:
		bResult = Kismet::LineTraceMultiForObjects(WorldContext, Start, End, ObjectTypes, false, IgnoreActors, Draw,
		                                           Hits, false);
		break;
	case 119:
		bResult =
		    Kismet::SphereTraceSingle(WorldContext, Start, End, 12, Channel, false, IgnoreActors, Draw, Hit, false);
		break;
	case 120:
		bResult =
		    Kismet::SphereTraceMulti(WorldContext, Start, End, 12, Channel, false, IgnoreActors, Draw, Hits, false);
		break;
	case 121:
		bResult = Kismet::SphereTraceSingleForObjects(WorldContext, Start, End, 12, ObjectTypes, false, IgnoreActors,
		                                              Draw, Hit, false);
		break;
	case 122:
		bResult = Kismet::SphereTraceMultiForObjects(WorldContext, Start, End, 12, ObjectTypes, false, IgnoreActors,
		                                             Draw, Hits, false);
		break;
	case 123:
		bResult = Kismet::BoxTraceSingle(WorldContext, Start, End, Extent, Zero, Channel, false, IgnoreActors, Draw,
		                                 Hit, false);
		break;
	case 124:
		bResult = Kismet::BoxTraceMulti(WorldContext, Start, End, Extent, Zero, Channel, false, IgnoreActors, Draw,
		                                Hits, false);
		break;
	case 125:
		bResult = Kismet::BoxTraceSingleForObjects(WorldContext, Start, End, Extent, Zero, ObjectTypes, false,
		                                           IgnoreActors, Draw, Hit, false);
		break;
	case 126:
		bResult = Kismet::BoxTraceMultiForObjects(WorldContext, Start, End, Extent, Zero, ObjectTypes, false,
		                                          IgnoreActors, Draw, Hits, false);
		break;
	case 127:
		bResult = Kismet::CapsuleTraceSingle(WorldContext, Start, End, 12, 24, Channel, false, IgnoreActors, Draw, Hit,
		                                     false);
		break;
	case 128:
		bResult = Kismet::CapsuleTraceMulti(WorldContext, Start, End, 12, 24, Channel, false, IgnoreActors, Draw, Hits,
		                                    false);
		break;
	case 129:
		bResult = Kismet::CapsuleTraceSingleForObjects(WorldContext, Start, End, 12, 24, ObjectTypes, false,
		                                               IgnoreActors, Draw, Hit, false);
		break;
	case 130:
		bResult = Kismet::CapsuleTraceMultiForObjects(WorldContext, Start, End, 12, 24, ObjectTypes, false,
		                                              IgnoreActors, Draw, Hits, false);
		break;
	case 131:
		bResult = Kismet::SphereOverlapActors(WorldContext, Primitive->GetComponentLocation(), 90, ObjectTypes, nullptr,
		                                      IgnoreActors, Actors);
		break;
	case 132:
		bResult = Kismet::SphereOverlapComponents(WorldContext, Primitive->GetComponentLocation(), 90, ObjectTypes,
		                                          nullptr, IgnoreActors, Components);
		break;
	case 133:
		bResult = Kismet::BoxOverlapActors(WorldContext, Primitive->GetComponentLocation(), FVector(90), ObjectTypes,
		                                   nullptr, IgnoreActors, Actors);
		break;
	case 134:
		bResult = Kismet::BoxOverlapComponents(WorldContext, Primitive->GetComponentLocation(), FVector(90),
		                                       ObjectTypes, nullptr, IgnoreActors, Components);
		break;
	case 135:
		bResult = Kismet::CapsuleOverlapActors(WorldContext, Primitive->GetComponentLocation(), 90, 120, ObjectTypes,
		                                       nullptr, IgnoreActors, Actors);
		break;
	case 136:
		bResult = Kismet::CapsuleOverlapComponents(WorldContext, Primitive->GetComponentLocation(), 90, 120,
		                                           ObjectTypes, nullptr, IgnoreActors, Components);
		break;
	case 137:
		Primitive->GetOverlappingActors(Actors);
		break;
	case 138:
		Primitive->GetOverlappingComponents(Components);
		break;
	case 139:
		bResult = Primitive->IsOverlappingActor(OverlapTarget->GetOwner());
		break;
	case 140:
		bResult = Primitive->IsOverlappingComponent(OverlapTarget);
		break;
	case 141:
		FloatResult = Primitive->GetClosestPointOnCollision(Start, VectorResult, NAME_None);
		break;
	case 142:
		Primitive->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		break;
	case 143:
		Primitive->SetCollisionObjectType(ECC_PhysicsBody);
		break;
	case 144:
		Primitive->SetCollisionResponseToChannel(ECC_Visibility, ECR_Overlap);
		break;
	case 145:
		Primitive->SetGenerateOverlapEvents(false);
		break;
	case 146:
		bResult = Gameplay::FindCollisionUV(CollisionUV, 0, ScreenResult);
		break;
	case 147:
		Kismet::Delay(WorldContext, 0.001f, Latent);
		break;
	case 148:
		Kismet::RetriggerableDelay(WorldContext, 0.001f, Latent);
		break;
	case 149:
		Kismet::MoveComponentTo(Component, Position, Rotation, false, false, 0.001f, false, EMoveComponentAction::Move,
		                        Latent);
		break;
	case 150:
		Timer = Kismet::K2_SetTimer(WorldContext, TEXT("TimerCallback"), 60.0f, false);
		break;
	case 151:
		Timer = Kismet::K2_SetTimerDelegate(TimerCallback, 60.0f, false);
		break;
	case 152:
		Kismet::K2_ClearAndInvalidateTimerHandle(WorldContext, Timer);
		break;
	case 153:
		Kismet::K2_PauseTimerHandle(WorldContext, Timer);
		break;
	case 154:
		Kismet::K2_UnPauseTimerHandle(WorldContext, Timer);
		break;
	case 155:
		Kismet::LoadAsset(
		    WorldContext,
		    TSoftObjectPtr<UObject>(FSoftObjectPath(TEXT("/Game/Fixtures/BenchmarkAsset.BenchmarkAsset"))),
		    AssetCallback, Latent);
		break;
	case 157:
		Kismet::LoadAssetClass(
		    WorldContext,
		    TSoftClassPtr<UObject>(FSoftObjectPath(TEXT("/Game/Fixtures/BP_BenchmarkWidget.BP_BenchmarkWidget_C"))),
		    ClassCallback, Latent);
		break;
	case 160:
		Gameplay::LoadStreamLevel(WorldContext, Level, true, false, Latent);
		break;
	case 161:
		Gameplay::UnloadStreamLevel(WorldContext, Level, Latent, false);
		break;
	case 162:
		Result = UAsyncActionHandleSaveGame::AsyncSaveGameToSlot(WorldContext, Save.Get(), SlotName, 0);
		break;
	case 163:
		Result = UAsyncActionHandleSaveGame::AsyncLoadGameFromSlot(WorldContext, SlotName, 0);
		break;
	case 164:
		Trace = World.AsyncLineTraceByChannel(EAsyncTraceType::Single, Start, End, ECC_Visibility, Query, Response,
		                                      &TraceCallback);
		break;
	case 165:
		Trace = World.AsyncSweepByChannel(EAsyncTraceType::Single, Start, End, FQuat::Identity, ECC_Visibility,
		                                  FCollisionShape::MakeSphere(12), Query, Response, &TraceCallback);
		break;
	case 166:
		Trace = World.AsyncOverlapByChannel(Primitive->GetComponentLocation(), FQuat::Identity, ECC_Visibility,
		                                    FCollisionShape::MakeSphere(90), Query, Response, &OverlapCallback);
		break;
	case 167:
		Physics->SetSimulatePhysics(true);
		break;
	case 168:
		bResult = Physics->IsSimulatingPhysics();
		break;
	case 169:
		Physics->AddForce(Motion, NAME_None, true);
		break;
	case 170:
		Physics->AddImpulse(Motion, NAME_None, true);
		break;
	case 171:
		Physics->AddTorqueInRadians(Motion, NAME_None, true);
		break;
	case 172:
		Physics->SetPhysicsLinearVelocity(Motion, false, NAME_None);
		break;
	case 173:
		VectorResult = Physics->GetPhysicsLinearVelocity();
		break;
	case 174:
		Physics->SetPhysicsAngularVelocityInDegrees(Motion, false, NAME_None);
		break;
	case 175:
		VectorResult = Physics->GetPhysicsAngularVelocityInDegrees();
		break;
	case 176:
		Physics->SetEnableGravity(true);
		break;
	case 177:
		Result = AIHelper::CreateMoveToProxyObject(WorldContext, NavigationPawn, NavEnd, nullptr, 5.0f, false);
		break;
	case 178:
		Result = AIHelper::GetAIController(NavigationPawn);
		break;
	case 179:
		AIHelper::SimpleMoveToActor(AI, Other);
		break;
	case 180:
		AIHelper::SimpleMoveToLocation(AI, NavEnd);
		break;
	case 181:
		bResult =
		    Navigation::K2_GetRandomReachablePointInRadius(WorldContext, NavStart, VectorResult, NavigationRadius);
		break;
	case 182:
		bResult =
		    Navigation::K2_GetRandomLocationInNavigableRadius(WorldContext, NavStart, VectorResult, NavigationRadius);
		break;
	case 183:
		bResult = Navigation::K2_ProjectPointToNavigation(WorldContext, NavStart, VectorResult, nullptr, nullptr,
		                                                  FVector(100, 100, 300));
		break;
	case 184:
		Result = Navigation::FindPathToLocationSynchronously(WorldContext, NavStart, NavEnd, NavigationPawn);
		break;
	case 185:
		bResult = Navigation::NavigationRaycast(WorldContext, NavStart, NavEnd, VectorResult);
		break;
	case 186:
		CountResult = AI->MoveToActor(Other, 5.0f, false, true, true, nullptr, false);
		break;
	case 187:
		Widget->AddToViewport(2);
		break;
	case 188:
		Widget->RemoveFromParent();
		break;
	case 189:
		Widget->SetVisibility(ESlateVisibility::Collapsed);
		break;
	case 190:
		Text->SetText(FText::FromString(TEXT("Benchmark")));
		break;
	case 191:
		Image->SetBrushFromTexture(TextureAsset, true);
		break;
	case 192:
		Progress->SetPercent(0.75f);
		break;
	case 193:
		Widget->SetPositionInViewport(FVector2D(30, 25), false);
		break;
	case 194:
		UWidgetBlueprintLibrary::SetInputMode_UIOnlyEx(Controller, Widget.Get(), EMouseLockMode::DoNotLock, false);
		break;
	case 195:
		Result = Primitive->GetMaterial(0);
		break;
	case 196:
		Result = Primitive->CreateDynamicMaterialInstance(0, Material, NAME_None);
		break;
	case 197:
		DynamicMaterial->SetScalarParameterValue(Parameter, 0.75f);
		break;
	case 198:
		Gameplay::PlaySoundAtLocation(WorldContext, Sound, ActorStart, Zero, 0.005f, 1.0f, 0.0f);
		break;
	case 199:
		Audio->Play(0.0f);
		break;
	case 200:
		Result = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		    WorldContext, NiagaraAsset, ActorStart, Zero, FVector::OneVector, false, true, ENCPoolMethod::None, false);
		break;
	default:
		return false;
	}
	Elapsed = FPlatformTime::Cycles64() - Started;
	return true;
}
} // namespace Benchmark
