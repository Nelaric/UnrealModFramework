// Copyright (c) 2026 Nelaric Contributors
#include "BenchmarkFixtures.h"

#include "AIController.h"
#include "GameFramework/HUD.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "Trace/Trace.h"

namespace
{
#if !WITH_EDITOR && UE_TRACE_ENABLED
using FEndFrameDelegateBase = FDefaultDelegateUserPolicy::FMulticastDelegateExtras;

decltype(auto) GetEndFrameBindings(const FEndFrameDelegateBase& Delegate);

// UE does not expose the handle it registers for TraceLog on OnEndFrame.
template <auto Bindings> struct TEndFrameBindingsAccess
{
	friend decltype(auto) GetEndFrameBindings(const FEndFrameDelegateBase& Delegate)
	{
		return (Delegate.*Bindings);
	}
};

template struct TEndFrameBindingsAccess<&FEndFrameDelegateBase::InvocationList>;

void RemoveTraceUpdate()
{
	FSimpleMulticastDelegate& EndFrame = FCoreDelegates::OnEndFrame;
	const auto TraceUpdate = reinterpret_cast<UPTRINT>(&UE::Trace::Update);
	for (const auto& Binding : GetEndFrameBindings(EndFrame))
	{
		if (Binding.GetBoundProgramCounterForTimerManager() == TraceUpdate)
		{
			EndFrame.Remove(Binding.GetHandle());
			return;
		}
	}
}
#endif

class FPerformanceBenchmarkModule final : public FDefaultGameModuleImpl
{
public:
	void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();
#if !WITH_EDITOR && UE_TRACE_ENABLED
		if (FParse::Param(FCommandLine::Get(), TEXT("RenderOffScreen")))
		{
			// UE 5.6 opens the Trace control socket from the first frame update even with TraceAutoStart disabled.
			RemoveTraceUpdate();
		}
#endif
	}
};
} // namespace

IMPLEMENT_PRIMARY_GAME_MODULE(FPerformanceBenchmarkModule, PerformanceBenchmark, "PerformanceBenchmark");

void ABenchmarkBlueprintDriver::BeginSample()
{
	Sample.Begin();
}

void ABenchmarkBlueprintDriver::EndSample()
{
	Sample.End();
}

void ABenchmarkBlueprintDriver::CompleteSample()
{
	++Sample.Completed;
}

void ABenchmarkBlueprintDriver::ObserveObject(UObject* Value)
{
	Sample.Object = Value;
}

ABenchmarkCharacter::ABenchmarkCharacter()
{
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::Disabled;
	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
}

ABenchmarkGameMode::ABenchmarkGameMode()
{
	DefaultPawnClass = ABenchmarkCharacter::StaticClass();
	HUDClass = AHUD::StaticClass();
}
