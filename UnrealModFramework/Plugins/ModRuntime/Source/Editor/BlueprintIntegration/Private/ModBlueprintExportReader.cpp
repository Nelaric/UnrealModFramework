// Copyright (c) 2026 Nelaric Contributors
#include "ModBlueprintExportReader.h"

#include "ModBlueprintExports.h"
#include "ModExtensionPayload.h"
#include "Engine/Blueprint.h"
#include "K2Node_ModExtensionPoint.h"

namespace Mod::BlueprintIntegration
{
TArray<FExportedAPI> ReadAPIs(const UBlueprint& Blueprint)
{
	TArray<FExportedAPI> APIs;
	if (const auto* Exports = UModBlueprintExports::Find(&Blueprint))
	{
		for (const UModBlueprintAPIExport* Entry : Exports->APIs)
		{
			if (Entry)
			{
				APIs.Add({Entry->EntryGuid, Entry->PublicName, Entry->GetExportName(), Entry->ExportVersion,
				          Entry->Scope, Entry->Thread, Entry->FailureRule, Entry->Permissions});
			}
		}
	}
	return APIs;
}

TArray<FExportedExtension> ReadExtensions(const UBlueprint& Blueprint)
{
	TArray<FExportedExtension> Result;
	const auto* Exports = UModBlueprintExports::Find(&Blueprint);
	if (!Exports)
	{
		return Result;
	}
	TArray<UEdGraph*> Graphs;
	Blueprint.GetAllGraphs(Graphs);
	TSet<FGuid> Points;
	for (const UEdGraph* Graph : Graphs)
	{
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (const auto* Point = Cast<UK2Node_ModExtensionPoint>(Node); Point && Point->FindDeclaration(Blueprint))
			{
				const auto* Exec = Point->FindPin(TEXT("execute"), EGPD_Input);
				if (Exec && !Exec->LinkedTo.IsEmpty())
				{
					Points.Add(Point->Declaration);
				}
			}
		}
	}
	const auto Add = [&](const UModBlueprintExportEntry& Entry, EModBlueprintExtensionScalar Scalar,
	                     const UScriptStruct* Struct, const UClass* Object, FName ViewId) -> FExportedExtension&
	{
		auto& Record = Result.AddDefaulted_GetRef();
		Record.Guid = Entry.EntryGuid;
		Record.Name = Entry.GetExportName();
		Record.Version = Entry.ExportVersion;
		Record.bHasPoint = Points.Contains(Entry.EntryGuid);
		static const FName Properties[]{TEXT("FloatProperty"), TEXT("DoubleProperty"), TEXT("IntProperty"),
		                                TEXT("Int64Property"), TEXT("BoolProperty"),   TEXT("ByteProperty")};
		const auto Index = static_cast<uint8>(Scalar);
		Record.ScalarProperty = Index < UE_ARRAY_COUNT(Properties) ? Properties[Index] : NAME_None;
		if (Struct)
		{
			Record.ScalarProperty = TEXT("StructProperty");
			Record.StructPath = Struct->GetPathName();
			Record.StructLayout = Mod::Extension::DescribePayloadStruct(*Struct);
		}
		if (Object)
		{
			Record.ScalarProperty = TEXT("ObjectProperty");
			Record.ObjectPath = Object->GetPathName();
			Record.ObjectViewId = ViewId;
		}
		return Record;
	};
	for (const UModBlueprintHookExport* Entry : Exports->Hooks)
	{
		if (!Entry)
		{
			continue;
		}
		auto& Record = Add(*Entry, Entry->Scalar, Entry->ContextType, Entry->ObjectType.Get(), Entry->ObjectViewId);
		Record.bHook = true;
		Record.Permission = Entry->Permission;
		Record.bAuthorityOnly = Entry->bAuthorityOnly;
		Record.bSupportedPayload = Entry->ObjectType ? !Entry->ContextType && !Record.ObjectViewId.IsNone()
		                                             : !Entry->ContextType || !Record.StructLayout.IsEmpty();
		for (const auto Stage : Entry->AllowedStages)
		{
			Record.Stages.Add(StaticEnum<EModBlueprintHookStage>()->GetNameStringByValue(static_cast<int64>(Stage)));
		}
		for (const auto Operation : Entry->AllowedOperations)
		{
			Record.Operations.Add(
			    StaticEnum<EModBlueprintHookOperation>()->GetNameStringByValue(static_cast<int64>(Operation)));
		}
	}
	for (const UModBlueprintEventExport* Entry : Exports->Events)
	{
		if (Entry)
		{
			auto& Record = Add(*Entry, Entry->Scalar, Entry->PayloadType, Entry->ObjectType.Get(), Entry->ObjectViewId);
			Record.Permission = Entry->SubscribePermission;
			Record.bAuthorityOnly = Entry->bAuthorityOnly;
			Record.bSupportedPayload =
			    Entry->ObjectType ? !Entry->PayloadType && !Record.ObjectViewId.IsNone()
			                      : (Entry->PayloadType ? !Record.StructLayout.IsEmpty() : Entry->bScalarPayload);
			Record.Scope = Entry->SourceScope;
			Record.PublishRule = Entry->PublishRule;
			Record.bImmediateUnsubscribe = Entry->bImmediateUnsubscribe;
		}
	}
	return Result;
}
} // namespace Mod::BlueprintIntegration
