// Copyright (c) 2026 Nelaric Contributors
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "GameCharacter.h"

#include "Curves/CurveFloat.h"
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
#include "ModAPIBlueprint.h"
#include "ModExtensionSubsystem.h"
#include "ModHookTestWorld.h"
#include "ModInstance.h"
#include "UObject/Script.h"
#include "UObject/UnrealType.h"

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
	FDefinition Former = Definition;
	Former.Id = Former.Permission = TEXT("hook/game.character/adjust-health");
	Former.Signature = TEXT("previous-health-signature");
	Former.DispatchId = HookId;
	const bool bCompleted = FHookTestWorld::Run(
	    *World, {Former, Definition},
	    {Former.Id, Definition.Permission, FName(Former.Id.ToString() + TEXT(".ModifyInputs")),
	     FName(HookId.ToString() + TEXT(".Replace"))},
	    [&](UModExtensionSubsystem& Extensions, UModInstance& Owner)
	    {
		    const auto Register = [&](const FDefinition& Contract, EStage Stage, EOperation Operation, FName Key,
		                              THandlerCalls<float> Calls)
		    {
			    FHandler Handler;
			    Handler.Target = &Owner;
			    Handler.Stage = Stage;
			    Handler.Key = Key;
			    Handler.Operations = Operation;
			    Handler.SetCalls(MoveTemp(Calls));
			    return Extensions.Register(Owner, Contract.Id, Contract.Signature, MoveTemp(Handler));
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
		    FDefinition WrongSignature = Former;
		    WrongSignature.Signature = Definition.Signature;
		    TestTrue(TEXT("Former ID requires its original signature"),
		             Register(WrongSignature, EStage::Post, EOperation::None, TEXT("WrongSignature"), {}).GetError() ==
		                 API::EError::SignatureMismatch);
		    TestTrue(TEXT("The current Replace grant does not authorize the former ID"),
		             Register(Former, EStage::On, EOperation::Replace, TEXT("WrongGrant"), {}).GetError() ==
		                 API::EError::PermissionDenied);
		    const auto OldPre = Register(Former, EStage::Pre, EOperation::ModifyInputs, TEXT("Pre"), MoveTemp(Pre));
		    if (!TestTrue(TEXT("Register Pre using the former ID and grant"), OldPre.IsSuccess()) ||
		        !TestTrue(
		            TEXT("Register On using the current ID"),
		            Register(Definition, EStage::On, EOperation::Replace, TEXT("On"), MoveTemp(On)).IsSuccess()) ||
		        !TestTrue(
		            TEXT("Register Post using the current ID"),
		            Register(Definition, EStage::Post, EOperation::None, TEXT("Post"), MoveTemp(Post)).IsSuccess()))
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
		    const auto& Token = OldPre.GetValue();
		    TestTrue(TEXT("Former ID subscription is active"),
		             Extensions.GetSubscriptionStatus(Owner, Token) == ESubscriptionStatus::Active);
		    Extensions.Unregister(Owner, Token);
		    TestTrue(TEXT("Former ID subscription can be removed"),
		             Extensions.GetSubscriptionStatus(Owner, Token) == ESubscriptionStatus::Inactive);
		    return true;
	    });
	return TestTrue(TEXT("Hook dispatch fixture completed"), bCompleted);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHostBlueprintExportTest, "Nelaric.UnrealModFramework.Host.BlueprintExport",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHostBlueprintExportTest::RunTest(const FString&)
{
	using namespace API;
	UClass* Class = LoadObject<UClass>(
	    nullptr, TEXT("/Game/Blueprints/BP_ModBlueprintAPIExportTest.BP_ModBlueprintAPIExportTest_C"));
	if (!TestNotNull(TEXT("Saved Blueprint export class"), Class))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
	};
	UObject* Instance =
	    Class->IsChildOf<AActor>() ? World->SpawnActor<AActor>(Class) : NewObject<UObject>(World, Class);
	if (!TestNotNull(TEXT("Blueprint export instance"), Instance))
	{
		return false;
	}
	const FName Function(TEXT("ModAPI_TestModAPIExport"));
	FBlueprintParameter Parameter{TEXT("InputFloatParrm"), TEXT("DoubleProperty"), EBlueprintParameter::Input};
	const auto Binding = FBlueprintCall::Bind(*Class, *Class, {Function, MakeArrayView(&Parameter, 1)});
	if (!TestTrue(TEXT("Compiled Blueprint matches the exported API signature"), Binding.IsSuccess()))
	{
		return false;
	}
	const auto& Call = Binding.GetValue();
	FEditorScriptExecutionGuard Guard;
	const auto Result = Call->Process(
	    *Instance, [&](void* Buffer)
	    { CastFieldChecked<FDoubleProperty>(Call->GetParameter(0))->SetPropertyValue_InContainer(Buffer, 3.0); },
	    [](const void*) {});
	TestTrue(TEXT("Invoking the exported Blueprint API succeeds"), Result == EError::None);
	Parameter.PropertyClass = TEXT("FloatProperty");
	TestFalse(TEXT("A consumer with the wrong parameter type cannot bind"),
	          FBlueprintCall::Bind(*Class, *Class, {Function, MakeArrayView(&Parameter, 1)}).IsSuccess());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHostEventWorldLifecycleTest, "Nelaric.UnrealModFramework.Host.EventWorldLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHostEventWorldLifecycleTest::RunTest(const FString&)
{
	using namespace Extension;
	using API::EError;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
	};
	auto* Source = World->SpawnActor<AGameCharacter>();
	if (!TestNotNull(TEXT("Event source"), Source))
	{
		return false;
	}
	FDefinition Definition;
	Definition.Id = Definition.Permission = TEXT("event/game.character/lifecycle-test");
	Definition.Version = TEXT("1.0.0");
	Definition.Signature = TEXT("scalar-float-event");
	Definition.BlueprintClass = FSoftClassPath(AGameCharacter::StaticClass());
	const bool bCompleted =
	    FHookTestWorld::Run(*World, {Definition}, {Definition.Permission},
	                        [&](UModExtensionSubsystem& Extensions, UModInstance& Owner)
	                        {
		                        TArray<float> Observed;
		                        auto* Listener = NewObject<UCurveFloat>(World);
		                        FHandler Handler;
		                        Handler.Target = Listener;
		                        Handler.Key = TEXT("lifecycle-listener");
		                        THandlerCalls<float> Calls;
		                        Calls.Event = [&Observed](float Value)
		                        {
			                        Observed.Add(Value);
			                        return EError::None;
		                        };
		                        Handler.SetCalls(MoveTemp(Calls));
		                        const auto Token =
		                            Extensions.Register(Owner, Definition.Id, Definition.Signature, MoveTemp(Handler));
		                        if (!TestTrue(TEXT("Register a world-bound Event listener"), Token.IsSuccess()))
		                        {
			                        return false;
		                        }
		                        TestTrue(TEXT("Publish the Event through the Blueprint dispatch path"),
		                                 Extensions.PublishBlueprint(*Source, Definition.Id, 5.0f) == EError::None);
		                        TestTrue(TEXT("The Event delivers its payload"), Observed == TArray<float>{5.0f});
		                        Listener->MarkAsGarbage();
		                        Extensions.PublishBlueprint(*Source, Definition.Id, 7.0f);
		                        TestEqual(TEXT("An expired UObject cannot receive an Event"), Observed.Num(), 1);
		                        Extensions.Stop();
		                        TestTrue(TEXT("World shutdown rejects new Event publications"),
		                                 Extensions.PublishBlueprint(*Source, Definition.Id, 9.0f) == EError::NotReady);
		                        return true;
	                        });
	return TestTrue(TEXT("Event lifecycle fixture completed"), bCompleted);
}
} // namespace Mod::Tests

#endif
