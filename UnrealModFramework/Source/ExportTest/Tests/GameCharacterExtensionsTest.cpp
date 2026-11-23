// Copyright (c) 2026 Nelaric Contributors
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "GameCharacter.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopeExit.h"
#include "ModExtensionSubsystem.h"
#include "ModHookTestWorld.h"
#include "ModInstance.h"
#include "UObject/Script.h"

#include <limits>

namespace Mod::Tests
{
namespace
{
const FName BlueprintProbeName(TEXT("ProbeChangeHealth"));

UClass* CompileHealthProbe()
{
	auto* Blueprint = FKismetEditorUtilities::CreateBlueprint(
	    AGameCharacter::StaticClass(), GetTransientPackage(),
	    MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("HookHealthProbe")), BPTYPE_Normal,
	    UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
	auto* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, BlueprintProbeName, UEdGraph::StaticClass(),
	                                                    UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);
	TArray<UK2Node_FunctionEntry*> Entries;
	Graph->GetNodesOfClass(Entries);
	if (Entries.Num() != 1)
	{
		return nullptr;
	}
	FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
	auto* Call = Creator.CreateNode();
	Call->SetFromFunction(
	    AGameCharacter::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(AGameCharacter, ChangeHealth)));
	Creator.Finalize();
	const auto* Schema = Graph->GetSchema();
	Schema->TrySetDefaultValue(*Call->FindPinChecked(TEXT("Delta")), TEXT("-20.0"));
	if (!Schema->TryCreateConnection(Entries[0]->FindPinChecked(UEdGraphSchema_K2::PN_Then), Call->GetExecPin()))
	{
		return nullptr;
	}
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return Blueprint->Status == BS_Error ? nullptr : Blueprint->GeneratedClass;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameCharacterHealthTest, "Nelaric.UnrealModFramework.Host.ChangeHealth",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameCharacterHealthTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
	};
	AGameCharacter* Character = World->SpawnActor<AGameCharacter>();
	if (!TestNotNull(TEXT("Health test character"), Character))
	{
		return false;
	}

	TestEqual(TEXT("ChangeHealth applies damage"), Character->ChangeHealth(-25.0f), 75.0f);
	TestEqual(TEXT("NaN returns the current health"), Character->ChangeHealth(std::numeric_limits<float>::quiet_NaN()),
	          75.0f);
	TestEqual(TEXT("Infinity returns the current health"),
	          Character->ChangeHealth(std::numeric_limits<float>::infinity()), 75.0f);
	TestEqual(TEXT("Invalid inputs preserve the stored health"), Character->GetCurrentHealth(), 75.0f);
	TestEqual(TEXT("Damage clamps at zero"), Character->ChangeHealth(-100.0f), 0.0f);
	TestEqual(TEXT("Healing clamps at the maximum"), Character->AddHealth(125.0f), 100.0f);
	TestEqual(TEXT("ChangeHealth returns the stored health at the maximum"), Character->ChangeHealth(1.0f), 100.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameCharacterHookDispatchTest, "Nelaric.UnrealModFramework.Host.ChangeHealthDispatch",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameCharacterHookDispatchTest::RunTest(const FString&)
{
	using namespace Extension;
	constexpr float ReplacementValue = 42.0f;
	const FName HookId(TEXT("hook/game.character/change-health"));
	FDefinition Definition;
#if MOD_API_BOOTSTRAP
	// Install a real channel even in the bootstrap World to detect accidental dispatch.
	Definition.Id = HookId;
	Definition.Signature = TEXT("bootstrap-fixture");
	Definition.Permission = HookId;
	Definition.Kind = EKind::Hook;
	Definition.bPre = Definition.bOn = Definition.bPost = true;
	Definition.Operations = EOperation::ModifyInputs | EOperation::Replace;
#else
	TArray<FDefinition> Definitions;
	FGeneratedRegistration::DescribeAll(Definitions);
	const auto* Generated = Definitions.FindByPredicate([HookId](const auto& Entry) { return Entry.Id == HookId; });
	if (!TestNotNull(TEXT("Generated ChangeHealth registration"), Generated))
	{
		return false;
	}
	Definition = *Generated;
#endif
	UClass* ProbeClass = CompileHealthProbe();
	if (!TestNotNull(TEXT("Compiled Blueprint calling ChangeHealth"), ProbeClass))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
	};
	auto* Native = World->SpawnActor<AGameCharacter>();
	auto* Blueprint = World->SpawnActor<AGameCharacter>(ProbeClass);
	if (!TestNotNull(TEXT("Native health probe"), Native) || !TestNotNull(TEXT("Blueprint health probe"), Blueprint))
	{
		return false;
	}
	auto* BlueprintCall = Blueprint->FindFunction(BlueprintProbeName);
	if (!TestNotNull(TEXT("Blueprint probe entry"), BlueprintCall))
	{
		return false;
	}
	const auto RunBlueprint = [&]
	{
		FEditorScriptExecutionGuard Guard;
		Blueprint->ProcessEvent(BlueprintCall, nullptr);
	};
	TArray<TPair<EStage, float>> Trace;
	const bool bCompleted = FHookTestWorld::Run(
	    *World, Definition,
	    [&](UModExtensionSubsystem& Extensions, UModInstance& Owner)
	    {
		    const auto Register = [&](EStage Stage, EOperation Operation, FName Key, THandlerCalls<float> Calls)
		    {
			    FHandler Handler;
			    Handler.Target = &Owner;
			    Handler.Stage = Stage;
			    Handler.Key = Key;
			    Handler.Operations = Operation;
			    Handler.SetCalls(MoveTemp(Calls));
			    return TestTrue(
			        *Key.ToString(),
			        Extensions.Register(Owner, HookId, Definition.Signature, MoveTemp(Handler)).IsSuccess());
		    };
		    THandlerCalls<float> Pre;
		    Pre.Hook = [&](float Delta)
		    {
			    Trace.Emplace(EStage::Pre, Delta);
			    FHookDecision Decision;
			    Decision.Value = -7.0f;
			    return Decision;
		    };
		    THandlerCalls<float> On;
		    On.Replace = [&](float Delta)
		    {
			    Trace.Emplace(EStage::On, Delta);
			    return FCoreResult{ReplacementValue, API::EError::None, true};
		    };
		    THandlerCalls<float> Post;
		    Post.Hook = [&](float Result)
		    {
			    Trace.Emplace(EStage::Post, Result);
			    return FHookDecision{};
		    };
		    if (!Register(EStage::Pre, EOperation::ModifyInputs, TEXT("Pre modifies input"), MoveTemp(Pre)) ||
		        !Register(EStage::On, EOperation::Replace, TEXT("On replaces body"), MoveTemp(On)) ||
		        !Register(EStage::Post, EOperation::None, TEXT("Post observes result"), MoveTemp(Post)))
		    {
			    return false;
		    }
		    const float NativeResult = Native->ChangeHealth(-20.0f);
#if MOD_API_BOOTSTRAP
		    TestEqual(TEXT("Bootstrap C++ calls the original body"), NativeResult, 80.0f);
		    TestTrue(TEXT("Bootstrap C++ bypasses the installed handlers"), Trace.IsEmpty());
		    RunBlueprint();
		    TestEqual(TEXT("Bootstrap Blueprint calls the original body"), Blueprint->GetCurrentHealth(), 80.0f);
		    TestTrue(TEXT("Bootstrap Blueprint bypasses the installed handlers"), Trace.IsEmpty());
#else
		    const TArray<TPair<EStage, float>> Expected{
		        {EStage::Pre, -20.0f}, {EStage::On, -7.0f}, {EStage::Post, ReplacementValue}};
		    TestEqual(TEXT("C++ returns the On replacement value"), NativeResult, ReplacementValue);
		    TestEqual(TEXT("C++ replacement suppresses the original body"), Native->GetCurrentHealth(), 100.0f);
		    TestTrue(TEXT("C++ executes Pre, On, Post with the expected arguments"), Trace == Expected);
		    Trace.Reset();
		    RunBlueprint();
		    TestTrue(TEXT("Blueprint executes Pre, On, Post with the expected arguments"), Trace == Expected);
		    TestEqual(TEXT("Blueprint replacement suppresses the original body"), Blueprint->GetCurrentHealth(),
		              100.0f);
		    Trace.Reset();
		    const TArray<TPair<EStage, float>> SameFile{
		        {EStage::Pre, 10.0f}, {EStage::On, -7.0f}, {EStage::Post, ReplacementValue}};
		    TestEqual(TEXT("Same-file AddHealth returns the On replacement value"), Native->AddHealth(10.0f),
		              ReplacementValue);
		    TestTrue(TEXT("Same-file AddHealth also executes the Hook"), Trace == SameFile);
#endif
		    return true;
	    });
	return TestTrue(TEXT("Hook dispatch fixture completed"), bCompleted);
}
} // namespace Mod::Tests

#endif
