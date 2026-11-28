// Copyright (c) 2026 Nelaric Contributors
#include "BenchmarkModInstance.h"

#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, BenchmarkMod);

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
