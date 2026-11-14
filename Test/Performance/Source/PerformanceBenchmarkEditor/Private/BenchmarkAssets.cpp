// Copyright (c) 2026 Nelaric Contributors
#include "BenchmarkAssets.h"

#include "BenchmarkFixtures.h"
#include "BenchmarkGraph.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/StaticMeshActor.h"
#include "WidgetBlueprint.h"

namespace
{
bool SaveAsset(UObject& Asset)
{
	Asset.SetFlags(RF_Public | RF_Standalone);
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	const FString Extension =
	    Asset.IsA<UWorld>() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
	const FString File = FPackageName::LongPackageNameToFilename(Asset.GetOutermost()->GetName(), Extension);
	return UPackage::SavePackage(Asset.GetOutermost(), &Asset, *File, Args);
}

bool MakeMap(const TCHAR* Path, bool bMain)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, FName(FPackageName::GetShortName(Path)),
	                                    CreatePackage(Path), true, ERHIFeatureLevel::Num, nullptr, false);
	if (bMain)
	{
		World->SpawnActor<APlayerStart>(FVector(0, 0, 160), FRotator::ZeroRotator);
		auto* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, -50), FRotator::ZeroRotator);
		Floor->GetStaticMeshComponent()->SetStaticMesh(
		    LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Floor->SetActorScale3D(FVector(80, 80, 1));
		auto* Level = NewObject<ULevelStreamingDynamic>(World);
		Level->SetWorldAssetByPackageName(TEXT("/Game/BenchmarkSublevel"));
		Level->SetShouldBeLoaded(false);
		Level->SetShouldBeVisible(false);
		World->AddStreamingLevel(Level);
	}
	const bool bSaved = SaveAsset(*World);
	World->DestroyWorld(false);
	return bSaved;
}

} // namespace

int32 UBenchmarkAssetsCommandlet::Main(const FString&)
{
	if (!MakeMap(TEXT("/Game/BenchmarkSublevel"), false) || !MakeMap(TEXT("/Game/BenchmarkMap"), true))
	{
		return 1;
	}
	auto* Asset = NewObject<UBenchmarkAsset>(CreatePackage(TEXT("/Game/Fixtures/BenchmarkAsset")),
	                                         TEXT("BenchmarkAsset"), RF_Public | RF_Standalone);
	if (!SaveAsset(*Asset))
	{
		return 1;
	}
	const TCHAR* WidgetPackage = TEXT("/Game/Fixtures/BP_BenchmarkWidget");
	auto* Widget = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
	    UUserWidget::StaticClass(), CreatePackage(WidgetPackage), TEXT("BP_BenchmarkWidget"), BPTYPE_Normal,
	    UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
	if (Widget)
	{
		Widget->WidgetTree->RootWidget = Widget->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	}
	if (!Widget || !Benchmark::SaveBlueprint(*Widget))
	{
		return 1;
	}
	return Benchmark::SaveEngineCallBlueprint(*ABenchmarkBlueprintDriver::StaticClass(),
	                                          TEXT("/Game/BP_NativeBenchmark"))
	           ? 0
	           : 1;
}
