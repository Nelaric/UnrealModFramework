// Copyright (c) 2026 Nelaric Contributors
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && !WITH_EDITOR

#include "BenchmarkSession.h"
#include "Engine/Engine.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace Benchmark
{
class FRunBenchmark final : public IAutomationLatentCommand
{
public:
	FRunBenchmark(FAutomationTestBase& InTest, FString InReport) : Test(InTest), Report(MoveTemp(InReport))
	{
	}

	bool Update() override
	{
		if (!Session)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (Context.WorldType == EWorldType::Game && World && World->GetMapName() == TEXT("BenchmarkMap") &&
				    World->HasBegunPlay() && World->GetFirstPlayerController())
				{
					Session = MakeUnique<FSession>(*World, MoveTemp(Report));
					if (!Session->Initialize())
					{
						Test.AddError(Session->GetFailure());
						return true;
					}
					return false;
				}
			}
			if (FPlatformTime::Seconds() >= StartupDeadline)
			{
				Test.AddError(TEXT("The standalone benchmark map did not start"));
				return true;
			}
			return false;
		}
		if (Session->Tick())
		{
			return false;
		}
		if (!Session->Finish())
		{
			Test.AddError(Session->GetFailure());
		}
		Session.Reset();
		return true;
	}

private:
	FAutomationTestBase& Test;
	FString Report;
	TUniquePtr<FSession> Session;
	const double StartupDeadline = FPlatformTime::Seconds() + 60.0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCallBenchmark, "Nelaric.UnrealModFramework.Performance.CallBenchmark",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::PerfFilter)

bool FCallBenchmark::RunTest(const FString& Parameters)
{
	FString Report;
	if (!FParse::Value(FCommandLine::Get(), TEXT("BenchmarkReport="), Report) || Report.IsEmpty())
	{
		AddError(TEXT("Supply -BenchmarkReport with a destination for the Markdown report"));
		return false;
	}
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FRunBenchmark>(*this, MoveTemp(Report)));
	return true;
}
} // namespace Benchmark

#endif
