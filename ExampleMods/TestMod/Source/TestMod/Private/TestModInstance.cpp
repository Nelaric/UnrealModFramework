// Copyright (c) 2026 Nelaric Contributors
#include "TestModInstance.h"

#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"
#include "TestModPreview.h"
#include "UMFDevSDKAPI.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogTestModCallback, Log, All);

namespace
{
constexpr TCHAR MannyPath[] = TEXT("Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
}

UTestModInstance::UTestModInstance()
{
	if (HasStandalonePreviewContent())
	{
		static ConstructorHelpers::FObjectFinder<USkeletalMesh> Manny(*ContentPath(MannyPath));
		PreviewReplacementMesh = Manny.Object;
	}
}

bool UTestModInstance::HasStandalonePreviewContent()
{
	return !FPackageName::MountPointExists(TEXT("/TestMod/")) &&
	       FPackageName::DoesPackageExist(TEXT("/Game/BP_CallbackProbe"));
}

FString UTestModInstance::ContentPath(const TCHAR* RelativePath)
{
	const TCHAR* Root = FPackageName::MountPointExists(TEXT("/TestMod/")) ? TEXT("/TestMod/") : TEXT("/Game/");
	return FString(Root) + RelativePath;
}

UWorld* UTestModInstance::GetWorld() const
{
	const ATestModPreviewPawn* Pawn = Cast<ATestModPreviewPawn>(GetOuter());
	return Pawn ? Pawn->GetWorld() : Super::GetWorld();
}

bool UTestModInstance::ReplaceCharacterMeshWithManny(const FUMFDevSDKPlayerContext& PlayerContext)
{
	ATestModPreviewPawn* Pawn = Cast<ATestModPreviewPawn>(GetOuter());
	FModObject_T_f2e0514f60055d77 Character;
	EUMFDevSDKCallOutcome Outcome = EUMFDevSDKCallOutcome::Failed;
	int64 ErrorCode = 0;
	if (!Pawn)
	{
		UUMFDevSDKLibrary::GetLocalCharacter(PlayerContext, Character, Outcome, ErrorCode);
		if (Outcome != EUMFDevSDKCallOutcome::Succeeded)
		{
			UE_LOG(LogTestModCallback, Warning, TEXT("GetLocalCharacter failed with error %lld."), ErrorCode);
			return false;
		}
	}

	USkeletalMesh* ReplacementMesh = PreviewReplacementMesh;
	if (!ReplacementMesh)
	{
		ReplacementMesh = LoadObject<USkeletalMesh>(nullptr, *ContentPath(MannyPath));
	}
	if (!IsValid(ReplacementMesh))
	{
		UE_LOG(LogTestModCallback, Warning, TEXT("Could not load the Manny skeletal mesh."));
		return false;
	}

	if (Pawn)
	{
		return IsValid(Pawn) && Pawn->SetMesh(ReplacementMesh);
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
	UE_LOG(LogTestModCallback, Display,
	       TEXT("OnModLoadCompleted origin=%s mod=%s version=%s success=%d alreadyMounted=%d error=%d reason=%s"),
	       Result.bStartup ? TEXT("startup") : TEXT("manual"), *Result.ModId, *Result.Version, Result.bSucceeded,
	       Result.bAlreadyMounted, Result.ErrorCode, *Result.Reason);
	Super::OnModLoadCompleted(Result);
}
