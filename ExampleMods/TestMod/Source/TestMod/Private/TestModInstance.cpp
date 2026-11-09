// Copyright (c) 2026 Nelaric Contributors
#include "TestModInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogTestModCallback, Log, All);

void UTestModInstance::OnModLoadCompleted(const FModLoadNotification& Result)
{
	Super::OnModLoadCompleted(Result);
	UE_LOG(LogTestModCallback, Display,
	       TEXT("OnModLoadCompleted origin=%s mod=%s version=%s success=%d alreadyMounted=%d error=%d reason=%s"),
	       Result.bStartup ? TEXT("startup") : TEXT("manual"), *Result.ModId, *Result.Version, Result.bSucceeded,
	       Result.bAlreadyMounted, Result.ErrorCode, *Result.Reason);
}
