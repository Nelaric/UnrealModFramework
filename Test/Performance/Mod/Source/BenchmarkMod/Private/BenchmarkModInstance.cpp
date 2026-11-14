// Copyright (c) 2026 Nelaric Contributors
#include "BenchmarkModInstance.h"

#include "BenchmarkNative.inl"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, BenchmarkMod);

int64 UBenchmarkModInstance::GetSampleAddress()
{
	return static_cast<int64>(reinterpret_cast<UPTRINT>(&Sample));
}

int64 UBenchmarkModInstance::MeasureCpp(int32 Case, int64 ContextAddress)
{
	// Shared fixture state is borrowed for the duration of this game-thread call.
	auto& Context = *reinterpret_cast<Benchmark::FCallContext*>(static_cast<UPTRINT>(ContextAddress));
	uint64 Elapsed = 0;
	return Context.DispatchCpp(Case, Elapsed, this) ? static_cast<int64>(Elapsed) : -1;
}

void UBenchmarkModInstance::BeginSample()
{
	Sample.Begin();
}

void UBenchmarkModInstance::EndSample()
{
	Sample.End();
}

void UBenchmarkModInstance::CompleteSample()
{
	++Sample.Completed;
}

void UBenchmarkModInstance::ObserveObject(UObject* Value)
{
	Sample.Object = Value;
}
