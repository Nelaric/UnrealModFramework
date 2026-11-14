// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "K2Node_AsyncAction.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_LoadAsset.h"
#include "K2Node_Self.h"
#include "K2Node_VariableSet.h"
#include "UObject/UObjectIterator.h"

namespace Benchmark
{
template <class T> T* MakeNode(UEdGraph& Graph)
{
	T* Node = NewObject<T>(&Graph);
	Graph.AddNode(Node);
	Node->CreateNewGuid();
	Node->PostPlacedNewNode();
	return Node;
}

inline UK2Node_CallFunction* Call(UEdGraph& Graph, const UFunction& Function)
{
	auto* Node = MakeNode<UK2Node_CallFunction>(Graph);
	Node->SetFromFunction(&Function);
	Node->AllocateDefaultPins();
	return Node;
}

inline bool Connect(UEdGraphPin* From, UEdGraphPin* To)
{
	return From && To && GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(From, To);
}

inline UBlueprint* NewBlueprint(UClass& Parent, const TCHAR* PackagePath)
{
	return FKismetEditorUtilities::CreateBlueprint(&Parent, CreatePackage(PackagePath),
	                                               FName(FPackageName::GetShortName(PackagePath)), BPTYPE_Normal,
	                                               UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
}

inline bool SaveBlueprint(UBlueprint& Blueprint)
{
	FKismetEditorUtilities::CompileBlueprint(&Blueprint);
	if (Blueprint.Status == BS_Error || !Blueprint.GeneratedClass)
	{
		return false;
	}
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	const FString File = FPackageName::LongPackageNameToFilename(Blueprint.GetOutermost()->GetName(),
	                                                             FPackageName::GetAssetPackageExtension());
	return UPackage::SavePackage(Blueprint.GetOutermost(), &Blueprint, *File, Args);
}

inline bool AddInputs(UK2Node_CustomEvent& Event, UK2Node& Target, FName Skip = NAME_None)
{
	for (UEdGraphPin* Pin : Target.Pins)
	{
		if (Pin->Direction != EGPD_Input || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec ||
		    Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate || Pin->PinName == Skip ||
		    Pin->bNotConnectable || (Pin->PinName == UEdGraphSchema_K2::PN_Self && Pin->bHidden))
		{
			continue;
		}
		FEdGraphPinType Type = Pin->PinType;
		Type.bIsReference = false;
		Type.bIsConst = false;
		const FName EventName = Pin->PinName == UEdGraphSchema_K2::PN_Self ? FName(TEXT("Receiver")) : Pin->PinName;
		if (!Connect(Event.CreateUserDefinedPin(EventName, Type, EGPD_Output), Pin))
		{
			UE_LOG(LogTemp, Error, TEXT("Cannot bind input %s on %s"), *Pin->PinName.ToString(),
			       *Event.CustomFunctionName.ToString());
			return false;
		}
	}
	return true;
}

inline UK2Node_CustomEvent* Event(UEdGraph& Graph, FName Name)
{
	auto* Node = MakeNode<UK2Node_CustomEvent>(Graph);
	Node->CustomFunctionName = Name;
	Node->AllocateDefaultPins();
	return Node;
}

inline UFunction* FindEngineAPI(const TCHAR* Qualified)
{
	FString Owner, Name;
	FString(Qualified).Split(TEXT("::"), &Owner, &Name);
	Owner.RightChopInline(1);
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->GetName() == Owner && It->GetOutermost()->GetName().StartsWith(TEXT("/Script/")))
		{
			UFunction* Function = It->FindFunctionByName(FName(Name));
			return Function && Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure) ? Function
			                                                                                              : nullptr;
		}
	}
	return nullptr;
}

inline bool AddDelegate(UEdGraph& Graph, UEdGraphPin& Input, FName Callback)
{
	auto* Node = MakeNode<UK2Node_CreateDelegate>(Graph);
	Node->AllocateDefaultPins();
	auto* Self = MakeNode<UK2Node_Self>(Graph);
	Self->AllocateDefaultPins();
	if (!Connect(Self->FindPin(UEdGraphSchema_K2::PN_Self), Node->GetObjectInPin()) ||
	    !Connect(Node->GetDelegateOutPin(), &Input))
	{
		return false;
	}
	Node->SetFunction(Callback);
	return true;
}

inline bool ConnectExecOutputs(UK2Node& Node, UEdGraphPin* Target)
{
	bool bConnected = false;
	for (UEdGraphPin* Pin : Node.Pins)
	{
		if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			if (!Benchmark::Connect(Pin, Target))
			{
				return false;
			}
			bConnected = true;
		}
	}
	return bConnected;
}

inline bool AddEvent(UBlueprint& Blueprint, int32 Id, UFunction& Function)
{
	UEdGraph& Graph = *Blueprint.UbergraphPages[0];
	auto* Entry = Event(Graph, FName(*FString::Printf(TEXT("Blueprint%03d"), Id)));
	auto* Begin = Call(Graph, *Blueprint.ParentClass->FindFunctionByName(TEXT("BeginSample")));
	auto* End = Call(Graph, *Blueprint.ParentClass->FindFunctionByName(TEXT("EndSample")));
	auto* Complete = Call(Graph, *Blueprint.ParentClass->FindFunctionByName(TEXT("CompleteSample")));
	for (auto* SampleNode : {Begin, End, Complete})
	{
		if (UEdGraphPin* Self = SampleNode->FindPin(UEdGraphSchema_K2::PN_Self))
		{
			Self->DefaultObject = nullptr;
		}
	}
	auto* EntryPin = Entry->FindPin(UEdGraphSchema_K2::PN_Then);
	if (!Connect(EntryPin, Begin->GetExecPin()))
	{
		return false;
	}
	// Expand the engine's async and soft-asset nodes in an event graph so the real activation and continuation execute.
	if ((Id >= 158 && Id <= 159) || (Id >= 162 && Id <= 163))
	{
		auto* Async = MakeNode<UK2Node_AsyncAction>(Graph);
		Async->InitializeProxyFromFunction(&Function);
		Async->AllocateDefaultPins();
		return AddInputs(*Entry, *Async) &&
		       Connect(Begin->GetThenPin(), Async->FindPin(UEdGraphSchema_K2::PN_Execute)) &&
		       Connect(Async->FindPin(UEdGraphSchema_K2::PN_Then), End->GetExecPin()) &&
		       Connect(Async->FindPin(TEXT("Completed")), Complete->GetExecPin());
	}
	if (Id == 155 || Id == 157)
	{
		UClass* NodeClass = Id == 155
		                        ? UK2Node_LoadAsset::StaticClass()
		                        : FindObject<UClass>(nullptr, TEXT("/Script/BlueprintGraph.K2Node_LoadAssetClass"));
		if (!NodeClass)
		{
			return false;
		}
		auto* Asset = NewObject<UK2Node_LoadAsset>(&Graph, NodeClass);
		Graph.AddNode(Asset);
		Asset->CreateNewGuid();
		Asset->AllocateDefaultPins();
		return AddInputs(*Entry, *Asset) &&
		       Connect(Begin->GetThenPin(), Asset->FindPin(UEdGraphSchema_K2::PN_Execute)) &&
		       Connect(Asset->FindPin(UEdGraphSchema_K2::PN_Then), End->GetExecPin()) &&
		       Connect(Asset->FindPin(UEdGraphSchema_K2::PN_Completed), Complete->GetExecPin());
	}
	auto* Target = Call(Graph, Function);
	const bool bLatent = Function.HasMetaData(TEXT("Latent"));
	if (!AddInputs(*Entry, *Target, bLatent ? FName(TEXT("LatentInfo")) : NAME_None))
	{
		return false;
	}
	if (Id == 151 && (!Target->FindPin(TEXT("Delegate")) ||
	                  !AddDelegate(Graph, *Target->FindPin(TEXT("Delegate")), TEXT("TimerCallback"))))
	{
		return false;
	}
	if (Target->IsNodePure())
	{
		auto* Return = Target->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
		if (!Return)
		{
			for (UEdGraphPin* Pin : Target->Pins)
			{
				if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
				{
					Return = Pin;
					break;
				}
			}
		}
		if (!Return)
		{
			return false;
		}
		// Assignment consumes the pure result and avoids a second Blueprint function dispatch.
		const FName Variable(*FString::Printf(TEXT("Observed%03d"), Id));
		FEdGraphPinType Type = Return->PinType;
		Type.bIsConst = false;
		Type.bIsReference = false;
		if (!FBlueprintEditorUtils::AddMemberVariable(&Blueprint, Variable, Type))
		{
			return false;
		}
		auto* Store = MakeNode<UK2Node_VariableSet>(Graph);
		Store->VariableReference.SetSelfMember(Variable);
		Store->AllocateDefaultPins();
		return Connect(Begin->GetThenPin(), Store->FindPin(UEdGraphSchema_K2::PN_Execute)) &&
		       Connect(Return, Store->FindPin(Variable)) &&
		       Connect(Store->FindPin(UEdGraphSchema_K2::PN_Then), End->GetExecPin());
	}
	UEdGraphPin* Exec = Target->GetExecPin();
	if (Id == 149)
	{
		Exec = Target->FindPin(TEXT("Move"));
		if (!Exec)
		{
			for (UEdGraphPin* Pin : Target->Pins)
			{
				if (Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec &&
				    Pin->PinName.ToString().EndsWith(TEXT("Move")))
				{
					Exec = Pin;
					break;
				}
			}
		}
	}
	if (!Connect(Begin->GetThenPin(), Exec))
	{
		return false;
	}
	if (bLatent)
	{
		// The Blueprint continuation only verifies completion. C++ brackets the synchronous event submission.
		return Connect(Target->GetThenPin(), Complete->GetExecPin());
	}
	if (Id == 19 || Id == 200)
	{
		auto* Observe = Call(Graph, *Blueprint.ParentClass->FindFunctionByName(TEXT("ObserveObject")));
		if (UEdGraphPin* Self = Observe->FindPin(UEdGraphSchema_K2::PN_Self))
		{
			Self->DefaultObject = nullptr;
		}
		return Connect(Target->GetThenPin(), End->GetExecPin()) && Connect(End->GetThenPin(), Observe->GetExecPin()) &&
		       Connect(Target->FindPin(UEdGraphSchema_K2::PN_ReturnValue), Observe->FindPin(TEXT("Value")));
	}
	return ConnectExecOutputs(*Target, End->GetExecPin());
}

inline bool SaveEngineCallBlueprint(UClass& Parent, const TCHAR* PackagePath)
{
	UBlueprint* Blueprint = NewBlueprint(Parent, PackagePath);
	if (!Blueprint || Blueprint->UbergraphPages.IsEmpty())
	{
		return false;
	}
	bool bOk = true;
#define BENCHMARK_API(Id, Qualified)                                                                                   \
	if (UFunction* Function = FindEngineAPI(TEXT(Qualified)))                                                          \
	{                                                                                                                  \
		if (!AddEvent(*Blueprint, Id, *Function))                                                                      \
		{                                                                                                              \
			UE_LOG(LogTemp, Error, TEXT("Failed to generate Blueprint call %03d %s"), Id, TEXT(Qualified));            \
			bOk = false;                                                                                               \
		}                                                                                                              \
	}
#include "BenchmarkCatalog.inl"
#undef BENCHMARK_API
	return bOk && SaveBlueprint(*Blueprint);
}
} // namespace Benchmark
