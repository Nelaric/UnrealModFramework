// Copyright (c) 2026 Nelaric Contributors
#include "SModBlueprintExportPanel.h"

#include "BlueprintEditor.h"
#include "BlueprintEditorModule.h"
#include "BlueprintEditorTabs.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "ModAPIBlueprint.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "BlueprintModes/WidgetBlueprintApplicationMode.h"
#include "UMGEditorModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/STextBlock.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"

#define LOCTEXT_NAMESPACE "BlueprintIntegration"

namespace Mod::BlueprintIntegration
{
namespace
{
const FName ExportTabId(TEXT("ModBlueprintExport"));

class FExportTabFactory final : public FWorkflowTabFactory
{
public:
	explicit FExportTabFactory(TSharedPtr<FBlueprintEditor> InEditor)
	    : FWorkflowTabFactory(ExportTabId, InEditor), Editor(InEditor)
	{
		TabLabel = LOCTEXT("ExportTab", "蓝图Mod导出");
		TabIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.BlueprintCore");
		ViewMenuDescription = TabLabel;
		ViewMenuTooltip = LOCTEXT("ExportTabTooltip", "编辑当前蓝图的 API、Event 和 Hook 导出声明");
		bIsSingleton = true;
	}

	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override
	{
		if (Editor.IsValid())
		{
			return SNew(SModBlueprintExportPanel, Editor);
		}
		return SNew(STextBlock).Text(LOCTEXT("MissingEditor", "蓝图编辑器已关闭"));
	}

private:
	TWeakPtr<FBlueprintEditor> Editor;
};

TSharedRef<SDockTab> MakeExportDockTab(const TWeakPtr<FBlueprintEditor> Editor)
{
	const TSharedRef<SDockTab> Tab =
	    SNew(SDockTab).TabRole(ETabRole::PanelTab).Label(LOCTEXT("ExportTab", "蓝图Mod导出"));
	Tab->SetTabIcon(FAppStyle::GetBrush("ClassIcon.BlueprintCore"));
	if (Editor.IsValid())
	{
		Tab->SetContent(SNew(SModBlueprintExportPanel, Editor));
	}
	else
	{
		Tab->SetContent(SNew(STextBlock).Text(LOCTEXT("MissingEditor", "蓝图编辑器已关闭")));
	}
	return Tab;
}

bool OpenFallbackTab(const TWeakPtr<FBlueprintEditor> WeakEditor, const FName ToolkitName)
{
	const TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
	if (!Editor)
	{
		return false;
	}
	const TSharedPtr<FTabManager> TabManager = Editor->GetTabManager();
	if (!TabManager)
	{
		return false;
	}

	const bool bHadSpawner = TabManager->HasTabSpawner(ExportTabId);
	if (ToolkitName == FName(TEXT("BlueprintEditor")) && bHadSpawner)
	{
		return false;
	}
	if (!bHadSpawner)
	{
		TabManager
		    ->RegisterTabSpawner(ExportTabId, FOnSpawnTab::CreateLambda([WeakEditor](const FSpawnTabArgs&)
		                                                                { return MakeExportDockTab(WeakEditor); }))
		    .SetDisplayName(LOCTEXT("ExportTab", "蓝图Mod导出"))
		    .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.BlueprintCore"));
	}
	if (TabManager->FindExistingLiveTab(ExportTabId))
	{
		return false;
	}
	if (TabManager->FindExistingLiveTab(FBlueprintEditorTabs::MyBlueprintID))
	{
		TabManager->InsertNewDocumentTab(FBlueprintEditorTabs::MyBlueprintID, ExportTabId,
		                                 FTabManager::FLiveTabSearch(), MakeExportDockTab(WeakEditor));
	}
	else
	{
		TabManager->TryInvokeTab(ExportTabId);
	}
	return false;
}
} // namespace

class FBlueprintIntegrationModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		if (IsRunningCommandlet())
		{
			return;
		}
		FBlueprintEditorModule& BlueprintEditor =
		    FModuleManager::LoadModuleChecked<FBlueprintEditorModule>(TEXT("Kismet"));
		RegisterTabsHandle =
		    BlueprintEditor.OnRegisterTabsForEditor().AddRaw(this, &FBlueprintIntegrationModule::RegisterTabs);
		RegisterLayoutHandle =
		    BlueprintEditor.OnRegisterLayoutExtensions().AddRaw(this, &FBlueprintIntegrationModule::ExtendLayout);
		IUMGEditorModule& WidgetEditor = FModuleManager::LoadModuleChecked<IUMGEditorModule>(TEXT("UMGEditor"));
		RegisterWidgetTabsHandle =
		    WidgetEditor.OnRegisterTabsForEditor().AddRaw(this, &FBlueprintIntegrationModule::RegisterWidgetTabs);
		RegisterWidgetLayoutHandle =
		    WidgetEditor.OnRegisterLayoutExtensions().AddRaw(this, &FBlueprintIntegrationModule::ExtendWidgetLayout);
		if (GEditor)
		{
			BlueprintPreCompileHandle =
			    GEditor->OnBlueprintPreCompile().AddLambda([](UBlueprint*) { API::FBlueprintCall::Invalidate(); });
			if (UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				AssetEditorSubsystem = AssetEditors;
				AssetOpenedHandle =
				    AssetEditors->OnAssetOpenedInEditor().AddRaw(this, &FBlueprintIntegrationModule::OnAssetOpened);
			}
		}
	}

	virtual void ShutdownModule() override
	{
		if (GEditor)
		{
			GEditor->OnBlueprintPreCompile().Remove(BlueprintPreCompileHandle);
		}
		if (FModuleManager::Get().IsModuleLoaded(TEXT("Kismet")))
		{
			FBlueprintEditorModule& BlueprintEditor =
			    FModuleManager::GetModuleChecked<FBlueprintEditorModule>(TEXT("Kismet"));
			BlueprintEditor.OnRegisterTabsForEditor().Remove(RegisterTabsHandle);
			BlueprintEditor.OnRegisterLayoutExtensions().Remove(RegisterLayoutHandle);
		}
		if (FModuleManager::Get().IsModuleLoaded(TEXT("UMGEditor")))
		{
			IUMGEditorModule& WidgetEditor = FModuleManager::GetModuleChecked<IUMGEditorModule>(TEXT("UMGEditor"));
			WidgetEditor.OnRegisterTabsForEditor().Remove(RegisterWidgetTabsHandle);
			WidgetEditor.OnRegisterLayoutExtensions().Remove(RegisterWidgetLayoutHandle);
		}
		if (UAssetEditorSubsystem* AssetEditors = AssetEditorSubsystem.Get())
		{
			AssetEditors->OnAssetOpenedInEditor().Remove(AssetOpenedHandle);
		}
		for (const FTSTicker::FDelegateHandle& Handle : PendingTickers)
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Handle);
		}
	}

private:
	void RegisterTabs(FWorkflowAllowedTabSet& TabSet, FName ModeName, TSharedPtr<FBlueprintEditor> Editor)
	{
		if (Editor && !TabSet.GetFactory(ExportTabId))
		{
			TabSet.RegisterFactory(MakeShared<FExportTabFactory>(Editor));
		}
	}

	void ExtendLayout(FLayoutExtender& Extender)
	{
		Extender.ExtendLayout(FBlueprintEditorTabs::MyBlueprintID, ELayoutExtensionPosition::After,
		                      FTabManager::FTab(ExportTabId, ETabState::OpenedTab));
	}

	void ExtendWidgetLayout(FLayoutExtender& Extender)
	{
		ExtendLayout(Extender);
		Extender.ExtendLayout(FName(TEXT("WidgetDetails")), ELayoutExtensionPosition::After,
		                      FTabManager::FTab(ExportTabId, ETabState::OpenedTab));
	}

	void RegisterWidgetTabs(const FWidgetBlueprintApplicationMode& Mode, FWorkflowAllowedTabSet& TabSet)
	{
		if (Mode.GetModeName() == FName(TEXT("GraphName")))
		{
			return;
		}
		TSharedPtr<FBlueprintEditor> Editor = Mode.GetBlueprintEditor();
		if (Editor && !TabSet.GetFactory(ExportTabId))
		{
			TabSet.RegisterFactory(MakeShared<FExportTabFactory>(Editor));
		}
	}

	void OnAssetOpened(UObject* Asset, IAssetEditorInstance* Instance)
	{
		if (!Cast<UBlueprint>(Asset) || !Instance)
		{
			return;
		}
		const FName ToolkitName = Instance->GetEditorName();
		if (ToolkitName != FName(TEXT("BlueprintEditor")) && ToolkitName != FName(TEXT("WidgetBlueprintEditor")) &&
		    ToolkitName != FName(TEXT("AnimationBlueprintEditor")))
		{
			return;
		}
		FBlueprintEditor* Editor = static_cast<FBlueprintEditor*>(Instance);
		TWeakPtr<FBlueprintEditor> WeakEditor = StaticCastSharedRef<FBlueprintEditor>(Editor->AsShared());
		PendingTickers.Add(FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		    [WeakEditor, ToolkitName](float) { return OpenFallbackTab(WeakEditor, ToolkitName); })));
	}

	FDelegateHandle RegisterTabsHandle;
	FDelegateHandle RegisterLayoutHandle;
	FDelegateHandle RegisterWidgetTabsHandle;
	FDelegateHandle RegisterWidgetLayoutHandle;
	FDelegateHandle AssetOpenedHandle;
	FDelegateHandle BlueprintPreCompileHandle;
	TWeakObjectPtr<UAssetEditorSubsystem> AssetEditorSubsystem;
	TArray<FTSTicker::FDelegateHandle> PendingTickers;
};

} // namespace Mod::BlueprintIntegration

IMPLEMENT_MODULE(Mod::BlueprintIntegration::FBlueprintIntegrationModule, BlueprintIntegration);

#undef LOCTEXT_NAMESPACE
