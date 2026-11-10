// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "Widgets/SCompoundWidget.h"

class FBlueprintEditor;
class FMenuBuilder;
class FUICommandList;
class SGraphActionMenu;
class SSearchBox;
class STextEntryPopup;
class UBlueprint;
class UEdGraph;
class UModBlueprintExportEntry;
struct FCreateWidgetForActionData;
struct FEdGraphSchemaAction;
struct FGraphActionListBuilderBase;
struct FGraphActionNode;
struct FPropertyChangedEvent;

/// Blueprint Mod declarations using the native My Blueprint layout.
class SModBlueprintExportPanel final : public SCompoundWidget, public FSelfRegisteringEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SModBlueprintExportPanel)
	{
	}
	SLATE_END_ARGS()

	virtual ~SModBlueprintExportPanel() override;
	void Construct(const FArguments& InArgs, TWeakPtr<FBlueprintEditor> InEditor);
	virtual void Tick(const FGeometry& AllottedGeometry, double CurrentTime, float DeltaTime) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override
	{
		return true;
	}
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

private:
	enum class EExportKind : int32
	{
		API = 1,
		Event,
		Hook,
	};

	UBlueprint* GetBlueprint() const;
	bool CanEdit() const;
	bool CanCreate(EExportKind Kind) const;
	bool IsEntryInBlueprint(const UModBlueprintExportEntry* Entry) const;
	bool CanEditSelectedEntry() const;
	bool CanOpenSelectedGraph() const;
	FText GetCreateTooltip(EExportKind Kind, FText Title) const;
	FReply AddEntry(EExportKind Kind);
	void AddEntryFromMenu(EExportKind Kind);
	void CreateNamedEntry(const FText& Name, ETextCommit::Type CommitType, EExportKind Kind);
	bool VerifyNewEntryName(const FText& Name, FText& ErrorMessage, EExportKind Kind) const;
	static UClass* GetEntryClass(EExportKind Kind);
	void SelectEntry(TWeakObjectPtr<UModBlueprintExportEntry> Entry);
	void ClearEntrySelection();
	void RemoveSelectedEntry();
	void RenameSelectedEntry();
	void OpenSelectedEntryGraph();
	void RenameEntry(const FText& NewText, ETextCommit::Type CommitType,
	                 TWeakObjectPtr<UModBlueprintExportEntry> Entry);
	bool VerifyEntryName(const FText& NewText, FText& ErrorMessage,
	                     TWeakObjectPtr<UModBlueprintExportEntry> Entry) const;
	void OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Change);
	void OnBlueprintChanged(UBlueprint* Blueprint);
	void RefreshAfterTransaction(bool bSuccess);
	void RebuildRows();
	void CollectAllActions(FGraphActionListBuilderBase& Actions);
	void CollectStaticSections(TArray<int32>& Sections);
	FText GetSectionTitle(int32 SectionId) const;
	TSharedRef<SWidget> GetSectionWidget(TSharedRef<SWidget> RowWidget, int32 SectionId);
	TSharedRef<SWidget> CreateActionWidget(FCreateWidgetForActionData* const CreateData);
	bool CanRenameAction(TWeakPtr<FGraphActionNode> Node) const;
	bool ActionMatchesName(FEdGraphSchemaAction* Action, const FName& Name) const;
	void OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& Actions, ESelectInfo::Type SelectInfo);
	void OnActionDoubleClicked(const TArray<TSharedPtr<FEdGraphSchemaAction>>& Actions);
	void OnFilterTextChanged(const FText& Text);
	FText GetFilterText() const;
	TSharedRef<SWidget> CreateAddMenu();
	void BuildAddMenu(FMenuBuilder& Menu);
	TSharedRef<SWidget> CreateViewOptionsMenu();
	void ToggleEmptySections();
	bool IsShowingEmptySections() const;
	TSharedPtr<SWidget> OnContextMenuOpening();
	void SetAllSectionsExpanded(bool bExpanded);
	static UEdGraph* GetEntryGraph(const UModBlueprintExportEntry* Entry);

	TWeakPtr<FBlueprintEditor> Editor;
	TWeakObjectPtr<UModBlueprintExportEntry> SelectedEntry;
	TSharedPtr<FUICommandList> CommandList;
	TSharedPtr<SSearchBox> FilterBox;
	TSharedPtr<SGraphActionMenu> GraphActionMenu;
	TWeakPtr<STextEntryPopup> NewEntryPopup;
	bool bShowEmptySections = true;
	bool bNeedsRefresh = false;
};
