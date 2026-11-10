// Copyright (c) 2026 Nelaric Contributors
#include "TestModInstance.h"

#include "Engine/SkeletalMesh.h"
#include "TestModPreview.h"
#include "UMFDevSDKAPI.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogTestModCallback, Log, All);

UTestModInstance::UTestModInstance()
{
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Manny(
	    TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	ReplacementMesh = Manny.Object;
}

UWorld* UTestModInstance::GetWorld() const
{
	const ATestModPreviewPawn* Pawn = Cast<ATestModPreviewPawn>(GetOuter());
	return Pawn ? Pawn->GetWorld() : Super::GetWorld();
}

bool UTestModInstance::ReplaceCharacterMeshWithManny(const FUMFDevSDKPlayerContext& PlayerContext)
{
	if (!IsValid(ReplacementMesh))
	{
		UE_LOG(LogTestModCallback, Warning, TEXT("Could not load the Manny skeletal mesh."));
		return false;
	}

	if (ATestModPreviewPawn* Pawn = Cast<ATestModPreviewPawn>(GetOuter()))
	{
		return IsValid(Pawn) && Pawn->SetMesh(ReplacementMesh);
	}

	FModObject_T_f2e0514f60055d77 Character;
	EUMFDevSDKCallOutcome Outcome = EUMFDevSDKCallOutcome::Failed;
	int64 ErrorCode = 0;
	UUMFDevSDKLibrary::GetLocalCharacter(PlayerContext, Character, Outcome, ErrorCode);
	if (Outcome != EUMFDevSDKCallOutcome::Succeeded)
	{
		UE_LOG(LogTestModCallback, Warning, TEXT("GetLocalCharacter failed with error %lld."), ErrorCode);
		return false;
	}

	bool bAccepted = false;
	UUMFDevSDKLibrary::SetMesh(Character, ReplacementMesh, bAccepted, Outcome, ErrorCode);
	if (Outcome != EUMFDevSDKCallOutcome::Succeeded || !bAccepted)
	{
		UE_LOG(LogTestModCallback, Warning, TEXT("SetMesh rejected Manny (error %lld)."), ErrorCode);
		return false;
	}
	return true;
}

void UTestModInstance::OnModLoadCompleted(const FModLoadNotification& Result)
{
	Super::OnModLoadCompleted(Result);
	UE_LOG(LogTestModCallback, Display,
	       TEXT("OnModLoadCompleted origin=%s mod=%s version=%s success=%d alreadyMounted=%d error=%d reason=%s"),
	       Result.bStartup ? TEXT("startup") : TEXT("manual"), *Result.ModId, *Result.Version, Result.bSucceeded,
	       Result.bAlreadyMounted, Result.ErrorCode, *Result.Reason);
}
