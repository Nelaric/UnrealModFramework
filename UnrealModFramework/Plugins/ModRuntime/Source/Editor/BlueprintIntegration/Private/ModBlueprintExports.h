// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "Blueprint/BlueprintExtension.h"
#include "CoreMinimal.h"

#include "ModBlueprintExports.generated.h"

class FKismetCompilerContext;
class UBlueprint;
class UEdGraph;
class UScriptStruct;

/// One declaration owned by a Blueprint's Mod export extension.
UCLASS(Abstract, MinimalAPI)
class UModBlueprintExportEntry : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FGuid EntryGuid;

	UPROPERTY(EditAnywhere, Category = "Mod Export")
	FString DisplayName;

	UPROPERTY(EditAnywhere, Category = "Mod Export", meta = (MultiLine = true))
	FText Description;

	UPROPERTY(EditAnywhere, Category = "Mod Export")
	FString ExportVersion;

	FName GetExportName() const;
	FText GetExportDisplayName() const;
	FText GetExportTooltip() const;
	void ApplyGraphNames(UEdGraph* Graph, const FString& SourceScope) const;
	UEdGraph* GetImplementationGraph() const;
	bool CanSetDisplayName(const FString& NewName, FText& ErrorMessage) const;
	bool SetDisplayName(const FString& NewName, FText& ErrorMessage);
	bool OwnsGraph(const UEdGraph* Graph) const;

	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

private:
	FString PreviousDisplayName;
};

/// A callable API declaration with its own implementation graph.
UCLASS(MinimalAPI)
class UModBlueprintAPIExport final : public UModBlueprintExportEntry
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "API")
	FString PublicName;

	UPROPERTY(EditAnywhere, Category = "API", meta = (GetOptions = "GetScopes"))
	FString Scope;

	UPROPERTY(EditAnywhere, Category = "API", meta = (GetOptions = "GetThreads"))
	FString Thread;

	UPROPERTY(EditAnywhere, Category = "API", meta = (GetOptions = "GetFailureRules"))
	FString FailureRule;

	UPROPERTY(EditAnywhere, Category = "API", meta = (GetOptions = "GetPermissions"))
	TArray<FString> Permissions;

	UPROPERTY()
	TObjectPtr<UEdGraph> ImplementationGraph;

	UFUNCTION()
	TArray<FString> GetScopes() const;

	UFUNCTION()
	TArray<FString> GetThreads() const;

	UFUNCTION()
	TArray<FString> GetFailureRules() const;

	UFUNCTION()
	TArray<FString> GetPermissions() const;
};

/// Supported Blueprint pin types for scalar extension contracts.
UENUM()
enum class EModBlueprintExtensionScalar : uint8
{
	Float,
	Double,
	Int32,
	Int64,
	Bool,
	Byte,
};

/// An event declaration published through the Mod event bus.
UCLASS(MinimalAPI)
class UModBlueprintEventExport final : public UModBlueprintExportEntry
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Event")
	TObjectPtr<UScriptStruct> PayloadType;

	UPROPERTY(EditAnywhere, Category = "Event")
	TSubclassOf<UObject> ObjectType;

	UPROPERTY(EditAnywhere, Category = "Event", meta = (EditCondition = "ObjectType != nullptr"))
	FName ObjectViewId;

	UPROPERTY(EditAnywhere, Category = "Event", meta = (GetOptions = "GetSourceScopes"))
	FString SourceScope;

	UPROPERTY(EditAnywhere, Category = "Event", meta = (GetOptions = "GetPublishRules"))
	FString PublishRule;

	UPROPERTY(EditAnywhere, Category = "Event")
	bool bImmediateUnsubscribe = false;

	UPROPERTY(EditAnywhere, Category = "Event")
	bool bScalarPayload = false;

	UPROPERTY(EditAnywhere, Category = "Event", meta = (EditCondition = "bScalarPayload"))
	EModBlueprintExtensionScalar Scalar = EModBlueprintExtensionScalar::Float;

	UPROPERTY(EditAnywhere, Category = "Event")
	FName SubscribePermission;

	UPROPERTY(EditAnywhere, Category = "Event")
	bool bAuthorityOnly = true;

	UFUNCTION()
	TArray<FString> GetSourceScopes() const;

	UFUNCTION()
	TArray<FString> GetPublishRules() const;
};

/// Stages that a Hook declaration can open to Mod handlers.
UENUM()
enum class EModBlueprintHookStage : uint8
{
	Pre,
	On,
	Post,
};

/// Operations that require an explicit grant on a Hook declaration.
UENUM()
enum class EModBlueprintHookOperation : uint8
{
	ModifyInputs UMETA(ToolTip = "Pre: Modify the inputs permitted by this Hook."),
	Cancel UMETA(ToolTip = "Pre: Cancel the operation before it executes."),
	Wrap UMETA(ToolTip = "On: Wrap execution with a continuation."),
	Replace UMETA(ToolTip = "On: Replace the original implementation."),
	ModifyResult UMETA(ToolTip = "Post: Modify the result permitted by this Hook."),
};

/// A Hook declaration with a typed context and original behavior graph.
UCLASS(MinimalAPI)
class UModBlueprintHookExport final : public UModBlueprintExportEntry
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Hook")
	TObjectPtr<UScriptStruct> ContextType;

	UPROPERTY(EditAnywhere, Category = "Hook")
	TSubclassOf<UObject> ObjectType;

	UPROPERTY(EditAnywhere, Category = "Hook", meta = (EditCondition = "ObjectType != nullptr"))
	FName ObjectViewId;

	UPROPERTY(EditAnywhere, Category = "Hook")
	EModBlueprintExtensionScalar Scalar = EModBlueprintExtensionScalar::Float;

	UPROPERTY(EditAnywhere, Category = "Hook")
	TArray<EModBlueprintHookStage> AllowedStages;

	UPROPERTY(EditAnywhere, Category = "Hook")
	TArray<EModBlueprintHookOperation> AllowedOperations;

	UPROPERTY(EditAnywhere, Category = "Hook")
	FName Permission;

	UPROPERTY(EditAnywhere, Category = "Hook")
	bool bAuthorityOnly = true;

	UPROPERTY()
	TObjectPtr<UEdGraph> OriginalGraph;
};

/// Editor-owned declarations serialized inside a Blueprint asset.
UCLASS(MinimalAPI)
class UModBlueprintExports final : public UBlueprintExtension
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<TObjectPtr<UModBlueprintAPIExport>> APIs;

	UPROPERTY()
	TArray<TObjectPtr<UModBlueprintEventExport>> Events;

	UPROPERTY()
	TArray<TObjectPtr<UModBlueprintHookExport>> Hooks;

	static UModBlueprintExports* Find(const UBlueprint* Blueprint);
	static UModBlueprintExports* FindOrAdd(UBlueprint* Blueprint);
	static void RefreshSearchIndex(UBlueprint* Blueprint);
	static bool CanAddEntry(const UBlueprint* Blueprint, TSubclassOf<UModBlueprintExportEntry> EntryClass,
	                        const FString& DisplayName, FText& ErrorMessage);

	UModBlueprintAPIExport* AddAPI(UBlueprint* Blueprint, const FString& DisplayName, FText& ErrorMessage);
	UModBlueprintEventExport* AddEvent(UBlueprint* Blueprint, const FString& DisplayName, FText& ErrorMessage);
	UModBlueprintHookExport* AddHook(UBlueprint* Blueprint, const FString& DisplayName, FText& ErrorMessage);
	void RemoveEntry(UBlueprint* Blueprint, UModBlueprintExportEntry* Entry);

	virtual void GetAllGraphs(TArray<UEdGraph*>& Graphs) const override;

private:
	virtual void HandleGenerateFunctionGraphs(FKismetCompilerContext* CompilerContext) override;
	virtual FSearchData HandleGatherSearchData(const UBlueprint* OwningBlueprint) const override;
};
