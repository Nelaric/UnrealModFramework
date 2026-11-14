// Copyright (c) 2026 Nelaric Contributors
#include "BenchmarkModAssets.h"

#include "BenchmarkGraph.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, BenchmarkModEditor);

int32 UBenchmarkModAssetsCommandlet::Main(const FString&)
{
	UClass* Parent = LoadClass<UObject>(nullptr, TEXT("/Script/BenchmarkMod.BenchmarkModInstance"));
	return Parent && Benchmark::SaveEngineCallBlueprint(*Parent, TEXT("/Game/BP_BenchmarkMod")) ? 0 : 1;
}
