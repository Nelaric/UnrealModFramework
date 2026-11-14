// Copyright (c) 2026 Nelaric Contributors
#include "BenchmarkSession.h"

#include "GameFramework/AsyncActionHandleSaveGame.h"
#include "BenchmarkNative.inl"

namespace Benchmark
{
bool FSession::AfterDispatch(int32 Case)
{
	if (Case >= 164 && Case <= 166 && !Trace.IsValid())
	{
		Fail(TEXT("Async collision query was not queued"));
		return false;
	}
	if (IsCpp(ActivePath) && (Case == 162 || Case == 163))
	{
		auto* Action = Cast<UAsyncActionHandleSaveGame>(Result.Get());
		if (!Action)
		{
			Fail(TEXT("Save action did not create a proxy"));
			return false;
		}
		Proxy.Reset(Action);
		Action->Completed.AddDynamic(CallbackTarget.Get(), &UBenchmarkCallbacks::SaveComplete);
		Action->Activate();
	}
	return true;
}
} // namespace Benchmark
