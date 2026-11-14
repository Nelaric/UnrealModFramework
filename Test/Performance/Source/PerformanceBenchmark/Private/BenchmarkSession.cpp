// Copyright (c) 2026 Nelaric Contributors
#include "BenchmarkSession.h"

#include "Animation/AnimInstance.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "Engine/LatentActionManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/AsyncActionHandleSaveGame.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/Material.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "ModInstance.h"
#include "ModLoader.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "TimerManager.h"
#include "UnrealEngine.h"
#include "UObject/UObjectIterator.h"

namespace Benchmark
{
namespace
{
constexpr const TCHAR* Names[] = {
#define BENCHMARK_API(Id, Qualified) TEXT(Qualified),
#include "BenchmarkCatalog.inl"
#undef BENCHMARK_API
};

UStaticMeshComponent* AddCube(AActor& Owner, UStaticMesh& Cube, FVector Location, FVector Scale)
{
	auto* Mesh = NewObject<UStaticMeshComponent>(&Owner);
	Owner.AddInstanceComponent(Mesh);
	if (!Owner.GetRootComponent())
	{
		Owner.SetRootComponent(Mesh);
	}
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetStaticMesh(&Cube);
	Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Mesh->RegisterComponent();
	Mesh->SetWorldLocation(Location);
	Mesh->SetWorldScale3D(Scale);
	return Mesh;
}

USceneComponent* AddScene(AActor& Owner)
{
	auto* Scene = NewObject<USceneComponent>(&Owner);
	Owner.AddInstanceComponent(Scene);
	if (!Owner.GetRootComponent())
	{
		Owner.SetRootComponent(Scene);
	}
	Scene->PrimaryComponentTick.bCanEverTick = true;
	Scene->RegisterComponent();
	return Scene;
}

} // namespace

FSession::FSession(UWorld& InWorld, FString InReport) : FCallContext(InWorld), Report(MoveTemp(InReport))
{
	static_assert(UE_ARRAY_COUNT(Names) == CaseCount);
}

bool FSession::Initialize()
{
	FScopedConditionalWorldSwitcher WorldScope(&World);
	Controller = World.GetFirstPlayerController();
	Player = Cast<ABenchmarkCharacter>(Controller->GetPawn());
	if (!Player)
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Player = World.SpawnActor<ABenchmarkCharacter>(FVector(0, 0, 160), FRotator::ZeroRotator, Spawn);
		if (Player)
		{
			Controller->Possess(Player);
		}
	}
	UClass* DriverClass =
	    LoadClass<ABenchmarkBlueprintDriver>(nullptr, TEXT("/Game/BP_NativeBenchmark.BP_NativeBenchmark_C"));
	auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Player || !Controller->GetHUD() || !DriverClass || !Cube)
	{
		Fail(FString::Printf(TEXT("Fixture initialization: pawn=%d HUD=%d Blueprint=%d collision mesh=%d"),
		                     Player != nullptr, Controller->GetHUD() != nullptr, DriverClass != nullptr,
		                     Cube != nullptr));
		return false;
	}
	Driver = World.SpawnActor<ABenchmarkBlueprintDriver>(DriverClass);
	Actor = World.SpawnActor<AActor>();
	Other = World.SpawnActor<AActor>();
	auto* Subject = World.SpawnActor<AActor>();
	Component = AddScene(*Actor);
	Parent = AddScene(*Other);
	Actor->SetActorLocation(ActorStart);
	Other->SetActorLocation(OtherStart);
	Actor->Tags.Add(TEXT("Benchmark"));
	Actor->SetOwner(Other);
	Primitive = AddCube(*Subject, *Cube, FVector(600, 0, 120), FVector::OneVector);
	Physics = AddCube(*World.SpawnActor<AActor>(), *Cube, FVector(1800, 0, 300), FVector::OneVector);
	Physics->SetEnableGravity(false);
	Physics->SetSimulatePhysics(true);
	OverlapTarget = AddCube(*World.SpawnActor<AActor>(), *Cube, FVector(660, 0, 120), FVector::OneVector);
	OverlapTarget->SetCollisionResponseToAllChannels(ECR_Overlap);
	OverlapTarget->SetGenerateOverlapEvents(true);
	Primitive->SetGenerateOverlapEvents(true);
	Primitive->UpdateOverlaps();
	SparePawn = World.SpawnActor<ABenchmarkCharacter>(FVector(1000, 300, 100), FRotator::ZeroRotator);
	NavigationPawn = World.SpawnActor<ABenchmarkCharacter>(NavStart + FVector(0, 0, 100), FRotator::ZeroRotator);
	NavigationPawn->SetCanAffectNavigationGeneration(false, true);
	NavigationPawn->SpawnDefaultController();
	AI = Cast<AAIController>(NavigationPawn->GetController());
	Mesh = Player->GetMesh();
	Mesh->SetSkeletalMeshAsset(LoadObject<USkeletalMesh>(
	    nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple")));
	Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
	Anim = Mesh->GetAnimInstance();
	Sequence.Reset(
	    LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle")));
	if (!AI || !Anim || !Sequence.IsValid())
	{
		Fail(TEXT("Could not initialize the AI controller or animation fixtures"));
		return false;
	}
	Montage.Reset(
	    UAnimMontage::CreateSlotAnimationAsDynamicMontage(Sequence.Get(), TEXT("DefaultSlot"), 0.0f, 0.0f, 1.0f, 4));
	if (!Montage.IsValid())
	{
		Fail(TEXT("Could not create the animation montage fixture"));
		return false;
	}
	auto& MontageSection = Montage->CompositeSections.AddDefaulted_GetRef();
	MontageSection.SectionName = Section;
	MontageSection.Link(Montage.Get(), Montage->GetPlayLength() * 0.5f);
	auto* WidgetClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/Fixtures/BP_BenchmarkWidget.BP_BenchmarkWidget_C"));
	Widget.Reset(CreateWidget<UUserWidget>(Controller, WidgetClass));
	Text.Reset(NewObject<UTextBlock>(Widget.Get()));
	Image.Reset(NewObject<UImage>(Widget.Get()));
	Progress.Reset(NewObject<UProgressBar>(Widget.Get()));
	Texture.Reset(LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture")));
	Material =
	    LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!Material)
	{
		Fail(TEXT("Could not load the benchmark material"));
		return false;
	}
	Primitive->SetMaterial(0, Material);
	DynamicMaterial.Reset(UMaterialInstanceDynamic::Create(Material, Primitive));
	Sound = LoadObject<USoundWave>(nullptr, TEXT("/Engine/EngineSounds/1kSineTonePing.1kSineTonePing"));
	Audio = NewObject<UAudioComponent>(Actor);
	Actor->AddInstanceComponent(Audio);
	Audio->SetSound(Sound);
	Audio->SetVolumeMultiplier(0.005f);
	Audio->RegisterComponent();
	Niagara.Reset(
	    LoadObject<UNiagaraSystem>(nullptr, TEXT("/Niagara/DefaultAssets/Templates/Systems/RadialBurst.RadialBurst")));
	Streaming = UGameplayStatics::GetStreamingLevel(&World, TEXT("BenchmarkSublevel"));
	auto* SaveFixture = NewObject<UBenchmarkSave>(Driver);
	SaveFixture->Payload.Init(7, 4096);
	Save.Reset(SaveFixture);
	CallbackTarget.Reset(NewObject<UBenchmarkCallbacks>(Driver));
	AssetCallback.BindDynamic(CallbackTarget.Get(), &UBenchmarkCallbacks::AssetLoaded);
	ClassCallback.BindDynamic(CallbackTarget.Get(), &UBenchmarkCallbacks::ClassLoaded);
	TraceCallback.BindUObject(CallbackTarget.Get(), &UBenchmarkCallbacks::TraceComplete);
	OverlapCallback.BindUObject(CallbackTarget.Get(), &UBenchmarkCallbacks::OverlapComplete);
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldDynamic));
	IgnoreActors = {Player, Actor};
	FCollisionQueryParams UVParams(SCENE_QUERY_STAT(BenchmarkUV), true);
	UVParams.bReturnFaceIndex = true;
	World.LineTraceSingleByChannel(CollisionUV, Start, End, ECC_Visibility, UVParams);
	const bool bSaveReady = UGameplayStatics::SaveGameToSlot(Save.Get(), SlotName, 0);
	if (!Widget.IsValid() || !Texture.IsValid() || !Sound || !Niagara.IsValid() || !Streaming || !bSaveReady)
	{
		Fail(FString::Printf(
		    TEXT("Fixture initialization: widget=%d texture=%d sound=%d Niagara=%d streaming=%d save=%d"),
		    Widget.IsValid(), Texture.IsValid(), Sound != nullptr, Niagara.IsValid(), Streaming != nullptr,
		    bSaveReady));
		return false;
	}
	Widget->SetIsFocusable(true);
	auto* Volume = World.SpawnActor<ANavMeshBoundsVolume>(FVector(0, 0, 0), FRotator::ZeroRotator);
	auto* Bounds = NewObject<UBoxComponent>(Volume);
	Bounds->InitBoxExtent(FVector(3500, 3500, 500));
	Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Volume->AddInstanceComponent(Bounds);
	Bounds->RegisterComponent();
	Volume->SetRootComponent(Bounds);
	if (auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World))
	{
		Nav->OnNavigationBoundsUpdated(Volume);
		Nav->Build();
	}
	if (!PrepareMod())
	{
		return false;
	}
	UGameplayStatics::SetEnableWorldRendering(Driver, false);
	UE_LOG(LogTemp, Display, TEXT("Benchmark fixtures ready"));
	for (int32 Case = 1; Case <= CaseCount; ++Case)
	{
		const FName Entry(*FString::Printf(TEXT("Blueprint%03d"), Case));
		const bool bCpp = CanCallCpp(Case);
		const bool bNativeBlueprint = Driver->FindFunction(Entry) != nullptr;
		Available[Case - 1][0] = bCpp;
		Available[Case - 1][1] = bNativeBlueprint;
		if (bModRequired)
		{
			Available[Case - 1][2] = bCpp;
			Available[Case - 1][3] = Mod->FindFunction(Entry) != nullptr;
			if (Available[Case - 1][3] != bNativeBlueprint)
			{
				Fail(FString::Printf(TEXT("Host and mounted Mod Blueprint coverage differs for API %03d"), Case));
				return false;
			}
		}
	}
	BeginCase();
	return true;
}

bool FSession::PrepareMod()
{
	FString Package, Digest;
	bModRequired = FParse::Value(FCommandLine::Get(), TEXT("BenchmarkModFile="), Package);
	if (!bModRequired)
	{
		return true;
	}
	FParse::Value(FCommandLine::Get(), TEXT("BenchmarkRegistry="), Digest);
	Mod::Loader::FLoadPolicy Policy;
	Policy.GameId = TEXT("PerformanceBenchmark");
	Policy.BaseReleaseVersion = TEXT("1.0.0");
	Policy.BaseRegistrySha256 = Digest;
	Policy.Backend = Mod::Loader::EBackend::Pak;
	Policy.bAllowNativeModules = true;
	const auto Load = Mod::Loader::IModLoaderModule::Get().LoadPackage(Package, Policy);
	if (!Load.IsSuccess() || Load.bAlreadyMounted)
	{
		Fail(FString::Printf(TEXT("Benchmark .mod could not be mounted: %s"), *Load.Detail));
		return false;
	}
	for (TObjectIterator<UModInstance> It; It; ++It)
	{
		if (It->GetOuter() == World.GetGameInstance() && It->GetModId() == TEXT("benchmarkmod"))
		{
			Mod = *It;
			break;
		}
	}
	if (!Mod.IsValid())
	{
		Fail(TEXT("Mounted Mod has no benchmark instance"));
		return false;
	}
	UFunction* GetSampleAddress = Mod->FindFunction(TEXT("GetSampleAddress"));
	ModNativeCall = Mod->FindFunction(TEXT("MeasureCpp"));
	if (!GetSampleAddress || !ModNativeCall)
	{
		Fail(TEXT("Mounted Mod is missing its measurement functions"));
		return false;
	}
	int64 SampleAddress = 0;
	Mod->ProcessEvent(GetSampleAddress, &SampleAddress);
	ModSample = reinterpret_cast<FBlueprintSample*>(static_cast<UPTRINT>(SampleAddress));
	if (!ModSample)
	{
		Fail(TEXT("Mounted Mod did not provide Blueprint measurement state"));
		return false;
	}
	return true;
}

void FSession::BeginCase()
{
	NumPaths = 0;
	Iteration = 0;
	PathIndex = 0;
	for (int32 Path = 0; Path < PathCount; ++Path)
	{
		if (Available[Id - 1][Path])
		{
			Paths[NumPaths++] = static_cast<EPath>(Path);
		}
	}
}

bool FSession::ReadyForNavigation()
{
	const FVector Extent(100, 100, 300);
	FVector ProjectedStart, ProjectedEnd;
	if (!UNavigationSystemV1::K2_ProjectPointToNavigation(Driver, NavStart, ProjectedStart, nullptr, nullptr, Extent) ||
	    !UNavigationSystemV1::K2_ProjectPointToNavigation(Driver, NavEnd, ProjectedEnd, nullptr, nullptr, Extent))
	{
		return false;
	}
	NavStart = ProjectedStart;
	NavEnd = ProjectedEnd;
	Other->SetActorLocation(NavEnd);
	return true;
}

void FSession::Fail(const FString& Reason)
{
	Failure = FString::Printf(TEXT("API %03d path %d: %s"), Id, static_cast<int32>(ActivePath), *Reason);
	bFailed = true;
}

bool FSession::Tick()
{
	if (bFailed || Id > CaseCount)
	{
		return false;
	}
	FScopedConditionalWorldSwitcher WorldScope(&World);
	if (Id == 177 && !bNavigationReady)
	{
		bNavigationReady = ReadyForNavigation();
		if (!bNavigationReady)
		{
			if (++ReadyFrames > 600)
			{
				Fail(TEXT("Navigation mesh was not ready"));
			}
			return !bFailed;
		}
	}
	const double Deadline = FPlatformTime::Seconds() + 0.005;
	do
	{
		if (NumPaths == 0 || Iteration == Samples)
		{
			if (Id % 10 == 0 || Id == 166)
			{
				UE_LOG(LogTemp, Display, TEXT("Benchmark completed through API %03d"), Id);
			}
			++Id;
			if (Id > CaseCount)
			{
				return false;
			}
			BeginCase();
			if (Id == 177)
			{
				return true;
			}
			continue;
		}
		const EPath Path = Paths[Iteration % 2 == 0 ? PathIndex : NumPaths - 1 - PathIndex];
		ActivePath = Path;
		const int32 Column = static_cast<int32>(Path);
		if (bPending)
		{
			if (IsPending(Id))
			{
				if (++WaitFrames > 600)
				{
					Fail(TEXT("Timed out waiting for the UE operation"));
				}
				return !bFailed;
			}
			bPending = false;
			if (!TearDown(Id))
			{
				return false;
			}
		}
		else
		{
			if (!SetUp(Id))
			{
				return false;
			}
			uint64 Elapsed = 0;
			const bool bDispatched = Path == EPath::NativeCpp ? DispatchCpp(Id, Elapsed, Driver)
			                         : Path == EPath::ModCpp  ? DispatchModCpp(Id, Elapsed)
			                                                  : DispatchBlueprint(Id, Elapsed);
			if (!bDispatched)
			{
				if (!bFailed)
				{
					Fail(TEXT("Missing UE call dispatch"));
				}
				return false;
			}
			if (!AfterDispatch(Id))
			{
				return false;
			}
			Cycles[Id - 1][Column] += Elapsed;
			++Counts[Id - 1][Column];
			if (IsPending(Id))
			{
				bPending = true;
				WaitFrames = 0;
				return true;
			}
			if (!TearDown(Id))
			{
				return false;
			}
		}
		if (++PathIndex == NumPaths)
		{
			PathIndex = 0;
			++Iteration;
		}
	} while (FPlatformTime::Seconds() < Deadline);
	return true;
}

bool FSession::WriteReport() const
{
	static const FString Heading = TEXT("| 原生 C++ | 原生蓝图 | Mod C++ | Mod 蓝图 |\n");
	static const FString Divider = TEXT("| ---: | ---: | ---: | ---: |\n");
	constexpr const TCHAR* Untested = TEXT(" 未测试 |");
	auto Cell = [](double Value) { return FString::Printf(TEXT(" %.3f |"), Value); };
	FString Rows;
	double Totals[PathCount]{};
	int32 Items[PathCount]{};
	for (int32 Case = 0; Case < CaseCount; ++Case)
	{
		Rows += FString::Printf(TEXT("| %03d | %s%s%s |"), Case + 1, TEXT("\x60"), Names[Case], TEXT("\x60"));
		for (int32 Column = 0; Column < PathCount; ++Column)
		{
			if (!Available[Case][Column])
			{
				Rows += Untested;
				continue;
			}
			if (Counts[Case][Column] != Samples)
			{
				return false;
			}
			const double Mean = FPlatformTime::ToSeconds64(Cycles[Case][Column]) * 1.0e9 / Samples;
			Rows += Cell(Mean);
			Totals[Column] += Mean;
			++Items[Column];
		}
		Rows += TEXT("\n");
	}
	FString Markdown = TEXT("# UE 调用性能基准（1000 次平均，单位 ns/次）\n\n") + Heading + Divider + TEXT("|");
	for (int32 Column = 0; Column < PathCount; ++Column)
	{
		Markdown += Items[Column] ? Cell(Totals[Column] / Items[Column]) : Untested;
	}
	Markdown += TEXT("\n\n| 编号 | API | 原生 C++ | 原生蓝图 | Mod C++ | Mod 蓝图 |\n");
	Markdown += TEXT("| --- | --- | ---: | ---: | ---: | ---: |\n");
	Markdown += Rows;
	Markdown.ReplaceInline(TEXT("\n"), TEXT("\r\n"), ESearchCase::CaseSensitive);
	return FFileHelper::SaveStringToFile(Markdown, *Report, FFileHelper::EEncodingOptions::ForceUTF8);
}

bool FSession::Finish()
{
	UGameplayStatics::DeleteGameInSlot(SlotName, 0);
	if (bFailed)
	{
		return false;
	}
	if (Id <= CaseCount || !WriteReport())
	{
		Fail(TEXT("The benchmark could not write a complete report"));
		return false;
	}
	return true;
}
} // namespace Benchmark
