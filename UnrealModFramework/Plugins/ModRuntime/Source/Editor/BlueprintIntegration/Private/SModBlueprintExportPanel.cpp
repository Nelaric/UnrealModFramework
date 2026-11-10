// Copyright (c) 2026 Nelaric Contributors
#include "SModBlueprintExportPanel.h"

#include "ModBlueprintExportGraphSchema.h"
#include "ModBlueprintExports.h"

#include "BlueprintEditor.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Blueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GraphActionNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "SGraphActionMenu.h"
#include "SGraphPalette.h"
#include "SKismetInspector.h"
#include "SPositiveActionButton.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/STextEntryPopup.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "ModBlueprintExportPanel"

namespace Mod::BlueprintIntegration
{
namespace
{
const FName ExportSelectionOwner(TEXT("ModBlueprintExport"));

FName GetExportId(const UModBlueprintExportEntry* Entry)
{
	if (const UModBlueprintAPIExport* API = Cast<UModBlueprintAPIExport>(Entry))
	{
		return API->ExportId;
	}
	if (const UModBlueprintEventExport* Event = Cast<UModBlueprintEventExport>(Entry))
	{
		return Event->ExportId;
	}
	if (const UModBlueprintHookExport* Hook = Cast<UModBlueprintHookExport>(Entry))
	{
		return Hook->ExportId;
	}
	return NAME_None;
}

const FSlateBrush* GetEntryIcon(const UModBlueprintExportEntry* Entry)
{
	if (Entry && Entry->IsA<UModBlueprintEventExport>())
	{
		return FAppStyle::GetBrush("GraphEditor.Delegate_16x");
	}
	if (Entry && Entry->IsA<UModBlueprintHookExport>())
	{
		return FAppStyle::GetBrush("GraphEditor.OverrideFunction_16x");
	}
	return FAppStyle::GetBrush("GraphEditor.Function_16x");
}

FText GetEntryTooltip(const UModBlueprintExportEntry* Entry)
{
	const FName ExportId = GetExportId(Entry);
	const FText IdText = ExportId.IsNone() ? LOCTEXT("UnassignedId", "尚未选择导出 ID") : FText::FromName(ExportId);
	FText Tooltip = FText::Format(LOCTEXT("EntryTooltip", "{0}\n{1}"), Entry->GetExportTooltip(), IdText);
	const UModBlueprintAPIExport* API = Cast<UModBlueprintAPIExport>(Entry);
	const UModBlueprintHookExport* Hook = Cast<UModBlueprintHookExport>(Entry);
	if ((API && !Entry->OwnsGraph(API->ImplementationGraph)) || (Hook && !Entry->OwnsGraph(Hook->OriginalGraph)))
	{
		Tooltip = FText::Format(LOCTEXT("MissingGraphTooltip", "{0}\n实现图缺失或归属已失效，无法打开。"), Tooltip);
	}
	return Tooltip;
}

FName GetEntryName(const UModBlueprintExportEntry* Entry)
{
	return Entry ? FName(*Entry->EntryGuid.ToString(EGuidFormats::Digits)) : NAME_None;
}

/// One native action-menu row referencing an independent declaration.
struct FExportSchemaAction final : FEdGraphSchemaAction
{
	FExportSchemaAction(UModBlueprintExportEntry* InEntry, const int32 SectionId)
	    : FEdGraphSchemaAction(FText::GetEmpty(), InEntry->GetExportDisplayName(), GetEntryTooltip(InEntry), 0,
	                           FText::Format(LOCTEXT("SearchKeywords", "{0} {1}"),
	                                         FText::FromString(InEntry->DisplayName),
	                                         FText::FromName(GetExportId(InEntry))),
	                           SectionId),
	      Entry(InEntry)
	{
	}

	static FName StaticGetTypeId()
	{
		static const FName TypeId(TEXT("ModBlueprintExportAction"));
		return TypeId;
	}

	virtual FName GetTypeId() const override
	{
		return StaticGetTypeId();
	}

	TWeakObjectPtr<UModBlueprintExportEntry> Entry;
};

/// Native palette text, icon spacing, and inline rename behavior.
class SExportPaletteItem final : public SGraphPaletteItem
{
public:
	SLATE_BEGIN_ARGS(SExportPaletteItem) : _ReadOnly(false)
	{
	}
	SLATE_ATTRIBUTE(bool, ReadOnly)
	SLATE_EVENT(FOnTextCommitted, OnNameCommitted)
	SLATE_EVENT(FOnVerifyTextChanged, OnVerifyName)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FCreateWidgetForActionData* const CreateData)
	{
		ActionPtr = CreateData->Action;
		NameCommitted = InArgs._OnNameCommitted;
		VerifyName = InArgs._OnVerifyName;
		const TSharedPtr<FExportSchemaAction> Action = StaticCastSharedPtr<FExportSchemaAction>(CreateData->Action);
		const TSharedRef<SWidget> IconWidget = CreateIconWidget(
		    Action->GetTooltipDescription(), GetEntryIcon(Action->Entry.Get()), FSlateColor::UseForeground());
		const TSharedRef<SWidget> NameWidget = CreateTextSlotWidget(CreateData, InArgs._ReadOnly);

		ChildSlot[SNew(SHorizontalBox) +
		          SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3.0f, 0.0f, 0.0f, 0.0f)[IconWidget] +
		          SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(3.0f, 3.0f)[NameWidget]];
	}

private:
	virtual bool OnNameTextVerifyChanged(const FText& NewText, FText& ErrorMessage) override
	{
		return VerifyName.IsBound() && VerifyName.Execute(NewText, ErrorMessage);
	}

	virtual void OnNameTextCommitted(const FText& NewText, ETextCommit::Type CommitType) override
	{
		NameCommitted.ExecuteIfBound(NewText, CommitType);
	}

	FOnTextCommitted NameCommitted;
	FOnVerifyTextChanged VerifyName;
};
} // namespace
} // namespace Mod::BlueprintIntegration

using namespace Mod::BlueprintIntegration;

void SModBlueprintExportPanel::Construct(const FArguments& InArgs, TWeakPtr<FBlueprintEditor> InEditor)
{
	Editor = InEditor;
	CommandList = MakeShared<FUICommandList>();
	CommandList->MapAction(FGenericCommands::Get().Rename,
	                       FExecuteAction::CreateSP(this, &SModBlueprintExportPanel::RenameSelectedEntry),
	                       FCanExecuteAction::CreateSP(this, &SModBlueprintExportPanel::CanEditSelectedEntry));
	CommandList->MapAction(FGenericCommands::Get().Delete,
	                       FExecuteAction::CreateSP(this, &SModBlueprintExportPanel::RemoveSelectedEntry),
	                       FCanExecuteAction::CreateSP(this, &SModBlueprintExportPanel::CanEditSelectedEntry));

	SAssignNew(FilterBox, SSearchBox).OnTextChanged(this, &SModBlueprintExportPanel::OnFilterTextChanged);

	SAssignNew(GraphActionMenu, SGraphActionMenu, false)
	    .OnGetFilterText(this, &SModBlueprintExportPanel::GetFilterText)
	    .OnCreateWidgetForAction(this, &SModBlueprintExportPanel::CreateActionWidget)
	    .OnCollectAllActions(this, &SModBlueprintExportPanel::CollectAllActions)
	    .OnCollectStaticSections(this, &SModBlueprintExportPanel::CollectStaticSections)
	    .OnActionSelected(this, &SModBlueprintExportPanel::OnActionSelected)
	    .OnActionDoubleClicked(this, &SModBlueprintExportPanel::OnActionDoubleClicked)
	    .OnContextMenuOpening(this, &SModBlueprintExportPanel::OnContextMenuOpening)
	    .OnCanRenameSelectedAction(this, &SModBlueprintExportPanel::CanRenameAction)
	    .OnGetSectionTitle(this, &SModBlueprintExportPanel::GetSectionTitle)
	    .OnGetSectionWidget(this, &SModBlueprintExportPanel::GetSectionWidget)
	    .OnActionMatchesName(this, &SModBlueprintExportPanel::ActionMatchesName)
	    .DefaultRowExpanderBaseIndentLevel(1)
	    .AlphaSortItems(false)
	    .UseSectionStyling(true);

	ChildSlot
	    [SNew(SVerticalBox) +
	     SVerticalBox::Slot().AutoHeight()
	         [SNew(SBorder).Padding(4.0f).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
	              [SNew(SHorizontalBox) +
	               SHorizontalBox::Slot()
	                   .AutoWidth()
	                   .VAlign(VAlign_Center)
	                   .Padding(0.0f, 0.0f, 4.0f,
	                            0.0f)[SNew(SPositiveActionButton)
	                                      .Icon(FAppStyle::GetBrush("Icons.Plus"))
	                                      .Text(NSLOCTEXT("MyBlueprint", "AddNewLabel", "Add"))
	                                      .ToolTipText(LOCTEXT("AddExportTooltip", "添加 API、Event 或 Hook 声明"))
	                                      .IsEnabled(this, &SModBlueprintExportPanel::CanEdit)
	                                      .OnGetMenuContent(this, &SModBlueprintExportPanel::CreateAddMenu)] +
	               SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[FilterBox.ToSharedRef()] +
	               SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f, 0.0f, 0.0f)
	                   [SNew(SComboButton)
	                        .ContentPadding(0.0f)
	                        .ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
	                        .HasDownArrow(false)
	                        .ToolTipText(LOCTEXT("ViewOptionsTooltip", "视图选项"))
	                        .OnGetMenuContent(this, &SModBlueprintExportPanel::CreateViewOptionsMenu)
	                        .ButtonContent()[SNew(SImage)
	                                             .ColorAndOpacity(FSlateColor::UseForeground())
	                                             .Image(FAppStyle::GetBrush("Icons.Settings"))]]]] +
	     SVerticalBox::Slot().FillHeight(1.0f)[GraphActionMenu.ToSharedRef()]];

	SetAllSectionsExpanded(true);
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SModBlueprintExportPanel::OnObjectPropertyChanged);
	if (UBlueprint* Blueprint = GetBlueprint())
	{
		Blueprint->OnChanged().AddSP(this, &SModBlueprintExportPanel::OnBlueprintChanged);
	}
}

SModBlueprintExportPanel::~SModBlueprintExportPanel()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
	if (UBlueprint* Blueprint = GetBlueprint())
	{
		Blueprint->OnChanged().RemoveAll(this);
	}
}

void SModBlueprintExportPanel::Tick(const FGeometry& AllottedGeometry, const double CurrentTime, const float DeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, CurrentTime, DeltaTime);
	if (const TSharedPtr<FBlueprintEditor> PinnedEditor = Editor.Pin();
	    SelectedEntry.IsValid() && PinnedEditor && PinnedEditor->GetUISelectionState() != ExportSelectionOwner)
	{
		SelectedEntry.Reset();
		GraphActionMenu->SelectItemByName(NAME_None);
	}
	if (bNeedsRefresh)
	{
		RebuildRows();
	}
}

FReply SModBlueprintExportPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent)
{
	return CommandList->ProcessCommandBindings(KeyEvent) ? FReply::Handled()
	                                                     : SCompoundWidget::OnKeyDown(MyGeometry, KeyEvent);
}

void SModBlueprintExportPanel::PostUndo(const bool bSuccess)
{
	RefreshAfterTransaction(bSuccess);
}

void SModBlueprintExportPanel::PostRedo(const bool bSuccess)
{
	RefreshAfterTransaction(bSuccess);
}

void SModBlueprintExportPanel::RefreshAfterTransaction(const bool bSuccess)
{
	if (bSuccess && GraphActionMenu)
	{
		ClearEntrySelection();
		UModBlueprintExports::RefreshSearchIndex(GetBlueprint());
		bNeedsRefresh = true;
	}
}

UBlueprint* SModBlueprintExportPanel::GetBlueprint() const
{
	const TSharedPtr<FBlueprintEditor> PinnedEditor = Editor.Pin();
	return PinnedEditor ? PinnedEditor->GetBlueprintObj() : nullptr;
}

bool SModBlueprintExportPanel::CanEdit() const
{
	const TSharedPtr<FBlueprintEditor> PinnedEditor = Editor.Pin();
	return PinnedEditor && PinnedEditor->InEditingMode();
}

bool SModBlueprintExportPanel::CanCreate(const EExportKind Kind) const
{
	const UBlueprint* Blueprint = GetBlueprint();
	if (!CanEdit() || !Blueprint || Blueprint->BlueprintType == BPTYPE_Interface ||
	    Blueprint->BlueprintType == BPTYPE_MacroLibrary)
	{
		return false;
	}
	return Kind == EExportKind::Event ? Blueprint->SupportsEventGraphs() || Blueprint->SupportsFunctions()
	                                  : Blueprint->SupportsFunctions();
}

bool SModBlueprintExportPanel::IsEntryInBlueprint(const UModBlueprintExportEntry* Entry) const
{
	const UModBlueprintExports* Exports = UModBlueprintExports::Find(GetBlueprint());
	if (!Exports || !Entry)
	{
		return false;
	}
	if (const UModBlueprintAPIExport* API = Cast<UModBlueprintAPIExport>(Entry))
	{
		return Exports->APIs.Contains(API);
	}
	if (const UModBlueprintEventExport* Event = Cast<UModBlueprintEventExport>(Entry))
	{
		return Exports->Events.Contains(Event);
	}
	if (const UModBlueprintHookExport* Hook = Cast<UModBlueprintHookExport>(Entry))
	{
		return Exports->Hooks.Contains(Hook);
	}
	return false;
}

bool SModBlueprintExportPanel::CanEditSelectedEntry() const
{
	return CanEdit() && IsEntryInBlueprint(SelectedEntry.Get());
}

bool SModBlueprintExportPanel::CanOpenSelectedGraph() const
{
	return IsEntryInBlueprint(SelectedEntry.Get()) && GetEntryGraph(SelectedEntry.Get());
}

FText SModBlueprintExportPanel::GetCreateTooltip(const EExportKind Kind, const FText Title) const
{
	return CanCreate(Kind) ? FText::Format(LOCTEXT("AddEntryTooltip", "新建 {0} 导出"), Title)
	                       : LOCTEXT("UnsupportedBlueprintType", "当前蓝图类型或编辑状态不能创建此类导出声明");
}

FReply SModBlueprintExportPanel::AddEntry(const EExportKind Kind)
{
	if (!CanCreate(Kind))
	{
		return FReply::Handled();
	}
	FSlateApplication::Get().DismissAllMenus();
	const TSharedRef<STextEntryPopup> Popup =
	    SNew(STextEntryPopup)
	        .Label(FText::Format(LOCTEXT("NewEntryName", "{0} 名称"), GetSectionTitle(static_cast<int32>(Kind))))
	        .DefaultText(GetSectionTitle(static_cast<int32>(Kind)))
	        .SelectAllTextWhenFocused(true)
	        .ClearKeyboardFocusOnCommit(false)
	        .OnVerifyTextChanged(this, &SModBlueprintExportPanel::VerifyNewEntryName, Kind)
	        .OnTextCommitted(this, &SModBlueprintExportPanel::CreateNamedEntry, Kind);
	NewEntryPopup = Popup;
	FSlateApplication::Get().PushMenu(SharedThis(this), FWidgetPath(), SNew(SBox).MinDesiredWidth(260.0f)[Popup],
	                                  FSlateApplication::Get().GetCursorPos(), FPopupTransitionEffect::TypeInPopup);
	return FReply::Handled();
}

void SModBlueprintExportPanel::CreateNamedEntry(const FText& Name, const ETextCommit::Type CommitType,
                                                const EExportKind Kind)
{
	if (CommitType != ETextCommit::OnEnter)
	{
		return;
	}
	FText ErrorMessage;
	if (!VerifyNewEntryName(Name, ErrorMessage, Kind))
	{
		if (const TSharedPtr<STextEntryPopup> Popup = NewEntryPopup.Pin())
		{
			Popup->SetError(ErrorMessage);
		}
		return;
	}
	UBlueprint* Blueprint = GetBlueprint();
	FScopedTransaction Transaction(LOCTEXT("AddExport", "Add Blueprint Mod export"));
	UModBlueprintExports* Exports = UModBlueprintExports::FindOrAdd(Blueprint);
	UModBlueprintExportEntry* Entry = nullptr;
	switch (Kind)
	{
	case EExportKind::API:
		Entry = Exports->AddAPI(Blueprint, Name.ToString(), ErrorMessage);
		break;
	case EExportKind::Event:
		Entry = Exports->AddEvent(Blueprint, Name.ToString(), ErrorMessage);
		break;
	case EExportKind::Hook:
		Entry = Exports->AddHook(Blueprint, Name.ToString(), ErrorMessage);
		break;
	}
	if (Entry)
	{
		FSlateApplication::Get().DismissAllMenus();
		NewEntryPopup.Reset();
		FilterBox->SetText(FText::GetEmpty());
		RebuildRows();
		TMap<int32, bool> Expansion;
		Expansion.Add(static_cast<int32>(Kind), true);
		GraphActionMenu->SetSectionExpansion(Expansion);
		GraphActionMenu->SelectItemByName(GetEntryName(Entry));
		SelectEntry(Entry);
	}
	else
	{
		Transaction.Cancel();
		if (const TSharedPtr<STextEntryPopup> Popup = NewEntryPopup.Pin())
		{
			Popup->SetError(ErrorMessage);
		}
	}
}

bool SModBlueprintExportPanel::VerifyNewEntryName(const FText& Name, FText& ErrorMessage, const EExportKind Kind) const
{
	return CanCreate(Kind) &&
	       UModBlueprintExports::CanAddEntry(GetBlueprint(), GetEntryClass(Kind), Name.ToString(), ErrorMessage);
}

UClass* SModBlueprintExportPanel::GetEntryClass(const EExportKind Kind)
{
	switch (Kind)
	{
	case EExportKind::API:
		return UModBlueprintAPIExport::StaticClass();
	case EExportKind::Event:
		return UModBlueprintEventExport::StaticClass();
	case EExportKind::Hook:
		return UModBlueprintHookExport::StaticClass();
	}
	return nullptr;
}

void SModBlueprintExportPanel::AddEntryFromMenu(const EExportKind Kind)
{
	AddEntry(Kind);
}

void SModBlueprintExportPanel::SelectEntry(TWeakObjectPtr<UModBlueprintExportEntry> Entry)
{
	UModBlueprintExportEntry* PreviousEntry = SelectedEntry.Get();
	SelectedEntry = IsEntryInBlueprint(Entry.Get()) ? Entry : nullptr;
	if (const TSharedPtr<FBlueprintEditor> PinnedEditor = Editor.Pin())
	{
		if (SelectedEntry.IsValid())
		{
			PinnedEditor->SetUISelectionState(ExportSelectionOwner);
			PinnedEditor->GetInspector()->ShowDetailsForSingleObject(
			    SelectedEntry.Get(),
			    SKismetInspector::FShowDetailsOptions(SelectedEntry->GetExportDisplayName(), true));
		}
		else if (PreviousEntry && PinnedEditor->GetInspector()->IsSelected(PreviousEntry))
		{
			PinnedEditor->GetInspector()->ShowDetailsForSingleObject(nullptr);
		}
	}
}

void SModBlueprintExportPanel::ClearEntrySelection()
{
	SelectEntry(nullptr);
	if (GraphActionMenu)
	{
		GraphActionMenu->SelectItemByName(NAME_None);
	}
}

void SModBlueprintExportPanel::RemoveSelectedEntry()
{
	if (!CanEditSelectedEntry())
	{
		return;
	}
	UBlueprint* Blueprint = GetBlueprint();
	UModBlueprintExports* Exports = UModBlueprintExports::Find(Blueprint);
	UModBlueprintExportEntry* Entry = SelectedEntry.Get();
	const FScopedTransaction Transaction(LOCTEXT("RemoveExport", "Remove Blueprint Mod export"));
	if (const TSharedPtr<FBlueprintEditor> PinnedEditor = Editor.Pin())
	{
		if (UEdGraph* Graph = GetEntryGraph(Entry); Graph && Graph->GetOuter() == Entry)
		{
			PinnedEditor->CloseDocumentTab(Graph);
		}
	}
	ClearEntrySelection();
	Exports->RemoveEntry(Blueprint, Entry);
	bNeedsRefresh = true;
}

void SModBlueprintExportPanel::RenameSelectedEntry()
{
	if (CanEditSelectedEntry())
	{
		GraphActionMenu->OnRequestRenameOnActionNode();
	}
}

void SModBlueprintExportPanel::OpenSelectedEntryGraph()
{
	if (const TSharedPtr<FBlueprintEditor> PinnedEditor = Editor.Pin(); PinnedEditor && CanOpenSelectedGraph())
	{
		UEdGraph* Graph = GetEntryGraph(SelectedEntry.Get());
		if (PinnedEditor->GetEditorName() == FName(TEXT("WidgetBlueprintEditor")))
		{
			PinnedEditor->SetCurrentMode(FName(TEXT("GraphName")));
		}
		PinnedEditor->OpenDocument(Graph, FDocumentTracker::OpenNewDocument);
	}
}

void SModBlueprintExportPanel::RenameEntry(const FText& NewText, const ETextCommit::Type CommitType,
                                           TWeakObjectPtr<UModBlueprintExportEntry> Entry)
{
	const FString NewName = NewText.ToString().TrimStartAndEnd();
	FText ErrorMessage;
	if (CommitType == ETextCommit::OnCleared || !VerifyEntryName(NewText, ErrorMessage, Entry))
	{
		return;
	}
	FScopedTransaction Transaction(LOCTEXT("RenameExport", "Rename Blueprint Mod export"));
	if (!Entry->SetDisplayName(NewName, ErrorMessage))
	{
		Transaction.Cancel();
		return;
	}
	SelectEntry(Entry);
	bNeedsRefresh = true;
}

bool SModBlueprintExportPanel::VerifyEntryName(const FText& NewText, FText& ErrorMessage,
                                               TWeakObjectPtr<UModBlueprintExportEntry> Entry) const
{
	return CanEdit() && IsEntryInBlueprint(Entry.Get()) && Entry->CanSetDisplayName(NewText.ToString(), ErrorMessage);
}

void SModBlueprintExportPanel::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Change)
{
	if (IsEntryInBlueprint(Cast<UModBlueprintExportEntry>(Object)))
	{
		if (Change.GetPropertyName() != GET_MEMBER_NAME_CHECKED(UModBlueprintExportEntry, DisplayName))
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
		}
		bNeedsRefresh = true;
	}
}

void SModBlueprintExportPanel::OnBlueprintChanged(UBlueprint* Blueprint)
{
	bNeedsRefresh = true;
}

void SModBlueprintExportPanel::RebuildRows()
{
	bNeedsRefresh = false;
	if (GraphActionMenu)
	{
		if (SelectedEntry.IsValid() && !IsEntryInBlueprint(SelectedEntry.Get()))
		{
			ClearEntrySelection();
		}
		GraphActionMenu->RefreshAllActions(true, false);
		if (SelectedEntry.IsValid())
		{
			GraphActionMenu->SelectItemByName(GetEntryName(SelectedEntry.Get()));
			if (const TSharedPtr<FBlueprintEditor> PinnedEditor = Editor.Pin();
			    PinnedEditor && PinnedEditor->GetInspector()->IsSelected(SelectedEntry.Get()))
			{
				SelectEntry(SelectedEntry);
			}
		}
	}
}

void SModBlueprintExportPanel::CollectAllActions(FGraphActionListBuilderBase& Actions)
{
	const UModBlueprintExports* Exports = UModBlueprintExports::Find(GetBlueprint());
	if (!Exports)
	{
		return;
	}
	for (UModBlueprintAPIExport* Entry : Exports->APIs)
	{
		if (Entry)
		{
			Actions.AddAction(MakeShared<FExportSchemaAction>(Entry, static_cast<int32>(EExportKind::API)));
		}
	}
	for (UModBlueprintEventExport* Entry : Exports->Events)
	{
		if (Entry)
		{
			Actions.AddAction(MakeShared<FExportSchemaAction>(Entry, static_cast<int32>(EExportKind::Event)));
		}
	}
	for (UModBlueprintHookExport* Entry : Exports->Hooks)
	{
		if (Entry)
		{
			Actions.AddAction(MakeShared<FExportSchemaAction>(Entry, static_cast<int32>(EExportKind::Hook)));
		}
	}
}

void SModBlueprintExportPanel::CollectStaticSections(TArray<int32>& Sections)
{
	if (bShowEmptySections)
	{
		Sections.Add(static_cast<int32>(EExportKind::API));
		Sections.Add(static_cast<int32>(EExportKind::Event));
		Sections.Add(static_cast<int32>(EExportKind::Hook));
	}
}

FText SModBlueprintExportPanel::GetSectionTitle(const int32 SectionId) const
{
	switch (static_cast<EExportKind>(SectionId))
	{
	case EExportKind::API:
		return LOCTEXT("APIHeader", "API");
	case EExportKind::Event:
		return LOCTEXT("EventHeader", "Event");
	case EExportKind::Hook:
		return LOCTEXT("HookHeader", "Hook");
	}
	return FText::GetEmpty();
}

TSharedRef<SWidget> SModBlueprintExportPanel::GetSectionWidget(TSharedRef<SWidget> RowWidget, const int32 SectionId)
{
	const EExportKind Kind = static_cast<EExportKind>(SectionId);
	return SNew(SButton)
	    .ButtonStyle(FAppStyle::Get(), "SimpleButton")
	    .ContentPadding(FMargin(1.0f, 0.0f))
	    .ToolTipText(this, &SModBlueprintExportPanel::GetCreateTooltip, Kind, GetSectionTitle(SectionId))
	    .IsEnabled(this, &SModBlueprintExportPanel::CanCreate, Kind)
	    .OnClicked(this, &SModBlueprintExportPanel::AddEntry, Kind)
	        [SNew(SImage).Image(FAppStyle::GetBrush("Icons.PlusCircle")).ColorAndOpacity(FSlateColor::UseForeground())];
}

TSharedRef<SWidget> SModBlueprintExportPanel::CreateActionWidget(FCreateWidgetForActionData* const CreateData)
{
	const TSharedPtr<FExportSchemaAction> Action = StaticCastSharedPtr<FExportSchemaAction>(CreateData->Action);
	return SNew(SExportPaletteItem, CreateData)
	    .ReadOnly_Lambda(
	        [WeakEditor = Editor]()
	        {
		        const TSharedPtr<FBlueprintEditor> PinnedEditor = WeakEditor.Pin();
		        return !PinnedEditor || !PinnedEditor->InEditingMode();
	        })
	    .OnNameCommitted(this, &SModBlueprintExportPanel::RenameEntry, Action->Entry)
	    .OnVerifyName(this, &SModBlueprintExportPanel::VerifyEntryName, Action->Entry);
}

bool SModBlueprintExportPanel::CanRenameAction(TWeakPtr<FGraphActionNode> Node) const
{
	return CanEdit() && Node.IsValid() && Node.Pin()->Action &&
	       Node.Pin()->Action->GetTypeId() == FExportSchemaAction::StaticGetTypeId();
}

bool SModBlueprintExportPanel::ActionMatchesName(FEdGraphSchemaAction* Action, const FName& Name) const
{
	if (Action && Action->GetTypeId() == FExportSchemaAction::StaticGetTypeId())
	{
		const UModBlueprintExportEntry* Entry = static_cast<FExportSchemaAction*>(Action)->Entry.Get();
		return Entry &&
		       (GetEntryName(Entry) == Name || Entry->GetExportName() == Name || FName(*Entry->DisplayName) == Name);
	}
	return false;
}

void SModBlueprintExportPanel::OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& Actions,
                                                const ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		SelectEntry(Actions.Num() > 0 && Actions[0]->GetTypeId() == FExportSchemaAction::StaticGetTypeId()
		                ? StaticCastSharedPtr<FExportSchemaAction>(Actions[0])->Entry
		                : nullptr);
	}
}

void SModBlueprintExportPanel::OnActionDoubleClicked(const TArray<TSharedPtr<FEdGraphSchemaAction>>& Actions)
{
	OnActionSelected(Actions, ESelectInfo::OnMouseClick);
	OpenSelectedEntryGraph();
}

void SModBlueprintExportPanel::OnFilterTextChanged(const FText& Text)
{
	if (GraphActionMenu)
	{
		GraphActionMenu->GenerateFilteredItems(false);
	}
}

FText SModBlueprintExportPanel::GetFilterText() const
{
	return FilterBox ? FilterBox->GetText() : FText::GetEmpty();
}

TSharedRef<SWidget> SModBlueprintExportPanel::CreateAddMenu()
{
	FMenuBuilder Menu(true, CommandList);
	BuildAddMenu(Menu);
	return Menu.MakeWidget();
}

void SModBlueprintExportPanel::BuildAddMenu(FMenuBuilder& Menu)
{
	Menu.BeginSection(TEXT("AddExport"), LOCTEXT("AddExportMenu", "添加导出"));
	for (const EExportKind Kind : {EExportKind::API, EExportKind::Event, EExportKind::Hook})
	{
		const FText Title = GetSectionTitle(static_cast<int32>(Kind));
		const FName IconName = Kind == EExportKind::Event  ? FName(TEXT("GraphEditor.Delegate_16x"))
		                       : Kind == EExportKind::Hook ? FName(TEXT("GraphEditor.OverrideFunction_16x"))
		                                                   : FName(TEXT("GraphEditor.Function_16x"));
		Menu.AddMenuEntry(Title, GetCreateTooltip(Kind, Title), FSlateIcon(FAppStyle::GetAppStyleSetName(), IconName),
		                  FUIAction(FExecuteAction::CreateSP(this, &SModBlueprintExportPanel::AddEntryFromMenu, Kind),
		                            FCanExecuteAction::CreateSP(this, &SModBlueprintExportPanel::CanCreate, Kind)));
	}
	Menu.EndSection();
}

TSharedRef<SWidget> SModBlueprintExportPanel::CreateViewOptionsMenu()
{
	FMenuBuilder Menu(true, CommandList);
	Menu.AddMenuEntry(LOCTEXT("ShowEmptySections", "显示空分类"),
	                  LOCTEXT("ShowEmptySectionsTooltip", "显示没有导出声明的分类"), FSlateIcon(),
	                  FUIAction(FExecuteAction::CreateSP(this, &SModBlueprintExportPanel::ToggleEmptySections),
	                            FCanExecuteAction(),
	                            FIsActionChecked::CreateSP(this, &SModBlueprintExportPanel::IsShowingEmptySections)),
	                  NAME_None, EUserInterfaceActionType::ToggleButton);
	Menu.AddMenuSeparator();
	Menu.AddMenuEntry(
	    LOCTEXT("ExpandAll", "展开所有分类"), FText::GetEmpty(), FSlateIcon(),
	    FUIAction(FExecuteAction::CreateSP(this, &SModBlueprintExportPanel::SetAllSectionsExpanded, true)));
	Menu.AddMenuEntry(
	    LOCTEXT("CollapseAll", "折叠所有分类"), FText::GetEmpty(), FSlateIcon(),
	    FUIAction(FExecuteAction::CreateSP(this, &SModBlueprintExportPanel::SetAllSectionsExpanded, false)));
	return Menu.MakeWidget();
}

void SModBlueprintExportPanel::ToggleEmptySections()
{
	bShowEmptySections = !bShowEmptySections;
	bNeedsRefresh = true;
}

bool SModBlueprintExportPanel::IsShowingEmptySections() const
{
	return bShowEmptySections;
}

TSharedPtr<SWidget> SModBlueprintExportPanel::OnContextMenuOpening()
{
	FMenuBuilder Menu(true, CommandList);
	if (IsEntryInBlueprint(SelectedEntry.Get()))
	{
		if (CanOpenSelectedGraph())
		{
			Menu.AddMenuEntry(
			    LOCTEXT("OpenGraph", "打开图"), LOCTEXT("OpenGraphTooltip", "打开这条声明的实现图"), FSlateIcon(),
			    FUIAction(FExecuteAction::CreateSP(this, &SModBlueprintExportPanel::OpenSelectedEntryGraph)));
			Menu.AddMenuSeparator();
		}
		Menu.AddMenuEntry(FGenericCommands::Get().Rename);
		Menu.AddMenuEntry(FGenericCommands::Get().Delete);
	}
	else
	{
		BuildAddMenu(Menu);
	}
	return Menu.MakeWidget();
}

void SModBlueprintExportPanel::SetAllSectionsExpanded(const bool bExpanded)
{
	TMap<int32, bool> Expansion;
	Expansion.Add(static_cast<int32>(EExportKind::API), bExpanded);
	Expansion.Add(static_cast<int32>(EExportKind::Event), bExpanded);
	Expansion.Add(static_cast<int32>(EExportKind::Hook), bExpanded);
	GraphActionMenu->SetSectionExpansion(Expansion);
}

UEdGraph* SModBlueprintExportPanel::GetEntryGraph(const UModBlueprintExportEntry* Entry)
{
	UEdGraph* Graph = Entry ? Entry->GetImplementationGraph() : nullptr;
	return Entry && Entry->OwnsGraph(Graph) && UModBlueprintExportGraphSchema::BindToDeclaration(Graph) ? Graph
	                                                                                                    : nullptr;
}

#undef LOCTEXT_NAMESPACE
