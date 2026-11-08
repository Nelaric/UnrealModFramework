// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "EdGraphSchema_K2.h"

#include "ModBlueprintExportGraphSchema.generated.h"

/// Native K2 behavior with declaration-based Mod graph display names.
UCLASS(MinimalAPI)
class UModBlueprintExportGraphSchema final : public UEdGraphSchema_K2
{
	GENERATED_BODY()

public:
	static bool BindToDeclaration(UEdGraph* Graph);
	virtual void GetGraphDisplayInformation(const UEdGraph& Graph, FGraphDisplayInfo& DisplayInfo) const override;
};
