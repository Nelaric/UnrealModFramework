// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "K2Node.h"

#include "K2Node_ModExtensionPoint.generated.h"

class UModBlueprintExportEntry;

UCLASS(MinimalAPI)
class UK2Node_ModExtensionPoint final : public UK2Node
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FGuid Declaration;

	UPROPERTY()
	bool bHook = false;

	void AllocateDefaultPins() override;
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	FText GetMenuCategory() const override;
	void GetMenuActions(FBlueprintActionDatabaseRegistrar& Registrar) const override;
	void ExpandNode(FKismetCompilerContext& Compiler, UEdGraph* Graph) override;
	bool IsCompatibleWithGraph(const UEdGraph* Graph) const override;
	bool IsNodePure() const override
	{
		return false;
	}
	const UModBlueprintExportEntry* FindDeclaration(const UBlueprint& Blueprint) const;
};
