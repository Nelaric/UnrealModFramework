// Copyright (c) 2026 Nelaric Contributors
#include "ModBlueprintExportGraphSchema.h"

#include "ModBlueprintExports.h"

#include "EdGraph/EdGraph.h"

namespace
{
const UModBlueprintExportEntry* GetGraphDeclaration(const UEdGraph* Graph)
{
	const UModBlueprintExportEntry* Entry = Graph ? Cast<UModBlueprintExportEntry>(Graph->GetOuter()) : nullptr;
	return Entry && Entry->OwnsGraph(Graph) && Entry->GetImplementationGraph() == Graph ? Entry : nullptr;
}
} // namespace

bool UModBlueprintExportGraphSchema::BindToDeclaration(UEdGraph* Graph)
{
	const UModBlueprintExportEntry* Entry = GetGraphDeclaration(Graph);
	if (!Entry)
	{
		return false;
	}
	Graph->Schema = StaticClass();
	Entry->ApplyGraphNames(Graph, Graph->GetName());
	return true;
}

void UModBlueprintExportGraphSchema::GetGraphDisplayInformation(const UEdGraph& Graph,
                                                                FGraphDisplayInfo& DisplayInfo) const
{
	Super::GetGraphDisplayInformation(Graph, DisplayInfo);
	if (const UModBlueprintExportEntry* Entry = GetGraphDeclaration(&Graph))
	{
		const FText Name = Entry->GetExportDisplayName();
		DisplayInfo.PlainName = Name;
		DisplayInfo.DisplayName = Name;
		DisplayInfo.Tooltip = Entry->GetExportTooltip();
	}
}
