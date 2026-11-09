// Copyright (c) 2026 Nelaric Contributors
#include "ModPackageEditor.h"

#include "Dom/JsonObject.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "IDetailsView.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "ModPackagingSettings.h"
#include "Packaging/ModPackageBuilder.h"
#include "PropertyEditorModule.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UnrealEdMisc.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ModPackageEditor"

DEFINE_LOG_CATEGORY_STATIC(LogModPackageEditor, Log, All);

namespace Mod::Editor
{
FModPackageEditor::~FModPackageEditor()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	if (TSharedPtr<SWindow> Window = PackagingWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}
	if (Process.IsValid())
	{
		FPlatformProcess::TerminateProc(Process, true);
	}
	CloseProcess();
	if (TSharedPtr<SNotificationItem> Item = Notification.Pin())
	{
		Item->SetCompletionState(SNotificationItem::CS_Fail);
		Item->ExpireAndFadeout();
	}
}

void FModPackageEditor::Startup()
{
	if (!IsRunningCommandlet())
	{
		UToolMenus::RegisterStartupCallback(
		    FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FModPackageEditor::RegisterMenus));
	}
}

void FModPackageEditor::RegisterMenus()
{
	FToolMenuOwnerScoped Owner(this);
	UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.User"));
	FToolMenuSection& Section = Toolbar->FindOrAddSection(TEXT("ModPackaging"));
	FToolMenuEntry Entry = FToolMenuEntry::InitComboButton(
	    TEXT("ModPackaging"), FUIAction(), FOnGetContent::CreateRaw(this, &FModPackageEditor::MakeMenu),
	    LOCTEXT("ToolbarLabel", "Mod"), LOCTEXT("ToolbarTip", "烘焙并打包 Mod，或查看打包输出。"),
	    FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("MainFrame.PackageProject")), false);
	Entry.StyleNameOverride = TEXT("AssetEditorToolbar");
	Section.AddEntry(Entry);
}

TSharedRef<SWidget> FModPackageEditor::MakeMenu()
{
	FMenuBuilder Menu(true, nullptr);
	Menu.AddMenuEntry(LOCTEXT("PackageMod", "打包 Mod…"),
	                  LOCTEXT("PackageModTip", "选择内容插件与基础发布，烘焙并生成 .mod 文件。"),
	                  FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("MainFrame.PackageProject")),
	                  FUIAction(FExecuteAction::CreateRaw(this, &FModPackageEditor::OpenPackagingDialog),
	                            FCanExecuteAction::CreateLambda([this] { return !Process.IsValid(); })));
	Menu.AddMenuEntry(LOCTEXT("OpenOutput", "打开输出目录"), LOCTEXT("OpenOutputTip", "打开 Mod 打包输出所在目录。"),
	                  FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.FolderOpen")),
	                  FUIAction(FExecuteAction::CreateRaw(this, &FModPackageEditor::OpenOutputDirectory)));
	Menu.AddMenuSeparator();
	Menu.AddMenuEntry(LOCTEXT("Cancel", "取消打包"), LOCTEXT("CancelTip", "停止当前烘焙与打包进程。"),
	                  FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.X")),
	                  FUIAction(FExecuteAction::CreateRaw(this, &FModPackageEditor::Cancel),
	                            FCanExecuteAction::CreateLambda([this] { return Process.IsValid(); })));
	return Menu.MakeWidget();
}

void FModPackageEditor::OpenPackagingDialog()
{
	if (TSharedPtr<SWindow> Existing = PackagingWindow.Pin())
	{
		Existing->BringToFront();
		return;
	}
	ValidationError = FText::GetEmpty();
	FDetailsViewArgs Args;
	Args.bAllowSearch = false;
	Args.bShowOptions = false;
	Args.bHideSelectionTip = true;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	TSharedRef<IDetailsView> Details =
	    FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(Args);
	Details->SetObject(GetMutableDefault<UModPackagingSettings>());
	TSharedRef<SWindow> Window = SNew(SWindow)
	                                 .Title(LOCTEXT("DialogTitle", "打包 Mod"))
	                                 .ClientSize(FVector2D(680, 590))
	                                 .SupportsMinimize(false)
	                                 .SupportsMaximize(false);
	PackagingWindow = Window;
	Window->SetContent(
	    SNew(SVerticalBox) +
	    SVerticalBox::Slot().AutoHeight().Padding(
	        16, 14)[SNew(STextBlock)
	                    .Text(LOCTEXT("Introduction", "选择已启用的项目内容插件。列表为空时，请先启用一个内容型 Mod "
	                                                  "插件。目标平台、基础发布和容器格式应与游戏 SDK 一致。"))
	                    .AutoWrapText(true)] +
	    SVerticalBox::Slot().FillHeight(1).Padding(8, 0)[Details] +
	    SVerticalBox::Slot().AutoHeight().Padding(
	        16, 8)[SNew(STextBlock)
	                   .Text_Lambda([this] { return ValidationError; })
	                   .ColorAndOpacity(FAppStyle::GetSlateColor(TEXT("Colors.Error")))
	                   .AutoWrapText(true)] +
	    SVerticalBox::Slot().AutoHeight()[SNew(SSeparator)] +
	    SVerticalBox::Slot()
	        .AutoHeight()
	        .HAlign(HAlign_Right)
	        .Padding(
	            16, 12)[SNew(SHorizontalBox) +
	                    SHorizontalBox::Slot().AutoWidth().Padding(
	                        0, 0, 8, 0)[SNew(SButton)
	                                        .Text(LOCTEXT("CloseDialog", "取消"))
	                                        .OnClicked_Lambda(
	                                            [WeakWindow = TWeakPtr<SWindow>(Window)]
	                                            {
		                                            if (TSharedPtr<SWindow> Pinned = WeakWindow.Pin())
		                                            {
			                                            Pinned->RequestDestroyWindow();
		                                            }
		                                            return FReply::Handled();
	                                            })] +
	                    SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
	                                                           .ButtonStyle(FAppStyle::Get(), TEXT("PrimaryButton"))
	                                                           .Text(LOCTEXT("StartPackaging", "开始打包"))
	                                                           .OnClicked_Lambda(
	                                                               [this, WeakWindow = TWeakPtr<SWindow>(Window)]
	                                                               {
		                                                               if (StartPackaging())
		                                                               {
			                                                               if (TSharedPtr<SWindow> Pinned =
			                                                                       WeakWindow.Pin())
			                                                               {
				                                                               Pinned->RequestDestroyWindow();
			                                                               }
		                                                               }
		                                                               return FReply::Handled();
	                                                               })]]);
	FSlateApplication::Get().AddWindow(Window);
}

bool FModPackageEditor::StartPackaging()
{
	if (Process.IsValid())
	{
		return false;
	}
	UModPackagingSettings* Settings = GetMutableDefault<UModPackagingSettings>();
	FModPackageRequest Request;
	if (!FModPackageRequest::FromSettings(*Settings, Request, ValidationError))
	{
		return false;
	}
	if (!FEditorFileUtils::SaveDirtyPackages(true, true, true, false, false, false))
	{
		ValidationError = LOCTEXT("SaveCancelled", "请先保存要打包的资产，再开始打包。");
		return false;
	}
	Settings->SaveConfig();
	WorkDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ModPackaging"),
	                                                                  FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	OutputFile = Request.OutputFile;
	const FString RequestFile = FPaths::Combine(WorkDirectory, TEXT("Request.json"));
	FString Json;
	FJsonSerializer::Serialize(Request.ToJson(), TJsonWriterFactory<>::Create(&Json));
	if (!IFileManager::Get().MakeDirectory(*WorkDirectory, true) ||
	    !FFileHelper::SaveStringToFile(Json, *RequestFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
	    !FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
	{
		CloseProcess();
		ValidationError = LOCTEXT("PrepareFailed", "无法创建打包任务，请检查项目 Saved 目录权限。");
		return false;
	}
	const FString LogFile = FPaths::Combine(WorkDirectory, TEXT("Editor.log"));
	const FString Arguments =
	    FString::Printf(TEXT("\"%s\" -run=ModPackage -Request=\"%s\" -abslog=\"%s\" ")
	                        TEXT("-unattended -nosplash -NullRHI -UTF8Output -stdout -FullStdOutLogOutput"),
	                    *FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()), *RequestFile, *LogFile);
	Process = FPlatformProcess::CreateProc(*FUnrealEdMisc::Get().GetExecutableForCommandlets(), *Arguments, false, true,
	                                       true, nullptr, 0, *FPaths::ProjectDir(), WritePipe, nullptr, WritePipe);
	if (!Process.IsValid())
	{
		CloseProcess();
		ValidationError = LOCTEXT("LaunchFailed", "无法启动 Mod 打包进程，请查看输出日志。");
		return false;
	}
	PendingLog.Reset();
	Deadline = FPlatformTime::Seconds() + Request.TimeoutMinutes * 60.0;
	FNotificationInfo Info(LOCTEXT("Cooking", "正在烘焙并打包 Mod… 可从 Mod 菜单取消。"));
	Info.bFireAndForget = false;
	Info.bUseThrobber = true;
	Info.bUseSuccessFailIcons = true;
	Notification = FSlateNotificationManager::Get().AddNotification(Info);
	if (TSharedPtr<SNotificationItem> Item = Notification.Pin())
	{
		Item->SetCompletionState(SNotificationItem::CS_Pending);
	}
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FModPackageEditor::Tick), 0.1f);
	UE_LOG(LogModPackageEditor, Display, TEXT("Packaging %s. Log: %s"), *Request.PluginName, *LogFile);
	return true;
}

bool FModPackageEditor::Tick(float DeltaTime)
{
	(void)DeltaTime;
	PendingLog += FPlatformProcess::ReadPipe(ReadPipe);
	FString Line;
	while (PendingLog.Split(TEXT("\n"), &Line, &PendingLog))
	{
		Line.TrimEndInline();
		UE_LOG(LogModPackageEditor, Display, TEXT("%s"), *Line);
		if (Line.Contains(TEXT("Verifying Mod package")))
		{
			if (TSharedPtr<SNotificationItem> Item = Notification.Pin())
			{
				Item->SetText(LOCTEXT("Verifying", "正在验证 .mod 包…"));
			}
		}
	}
	if (PendingLog.Len() > 32768)
	{
		UE_LOG(LogModPackageEditor, Display, TEXT("%s"), *PendingLog);
		PendingLog.Reset();
	}
	if (FPlatformProcess::IsProcRunning(Process))
	{
		if (FPlatformTime::Seconds() > Deadline)
		{
			FPlatformProcess::TerminateProc(Process, true);
			CloseProcess();
			Finish(LOCTEXT("TimedOut", "Mod 打包超时，已停止进程。请查看任务日志。"), false);
			TickHandle.Reset();
			return false;
		}
		return true;
	}
	int32 Code = 1;
	FPlatformProcess::GetProcReturnCode(Process, &Code);
	PendingLog += FPlatformProcess::ReadPipe(ReadPipe);
	if (!PendingLog.IsEmpty())
	{
		UE_LOG(LogModPackageEditor, Display, TEXT("%s"), *PendingLog);
	}
	CloseProcess();
	FString Text, Status, Artifact, Error;
	TSharedPtr<FJsonObject> Result;
	const bool bHasResult = FFileHelper::LoadFileToString(Text, *FPaths::Combine(WorkDirectory, TEXT("Result.json"))) &&
	                        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result) &&
	                        Result.IsValid();
	if (bHasResult)
	{
		Result->TryGetStringField(TEXT("status"), Status);
		Result->TryGetStringField(TEXT("artifact"), Artifact);
		Result->TryGetStringField(TEXT("error"), Error);
	}
	const bool bSuccess = Code == 0 && Status == TEXT("Packaged") && FPaths::IsSamePath(Artifact, OutputFile) &&
	                      FPaths::FileExists(OutputFile);
	if (bSuccess)
	{
		Finish(FText::Format(LOCTEXT("Completed", "Mod 打包完成：{0}"), FText::FromString(OutputFile)), true);
	}
	else
	{
		UE_LOG(LogModPackageEditor, Error, TEXT("Mod packaging failed (%d): %s. Log: %s/Editor.log"), Code, *Error,
		       *WorkDirectory);
		Finish(LOCTEXT("Failed", "Mod 打包失败。请查看输出日志中的错误与任务日志路径。"), false);
	}
	TickHandle.Reset();
	return false;
}

void FModPackageEditor::Cancel()
{
	if (!Process.IsValid())
	{
		return;
	}
	if (!FPlatformProcess::IsProcRunning(Process))
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		Tick(0.0f);
		return;
	}
	FPlatformProcess::TerminateProc(Process, true);
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	TickHandle.Reset();
	CloseProcess();
	Finish(LOCTEXT("Cancelled", "Mod 打包已取消。任务日志保留在 Saved/ModPackaging。"), false);
}

void FModPackageEditor::CloseProcess()
{
	if (Process.IsValid())
	{
		FPlatformProcess::CloseProc(Process);
		Process.Reset();
	}
	FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
	ReadPipe = nullptr;
	WritePipe = nullptr;
	PendingLog.Reset();
	if (!OutputFile.IsEmpty() && !WorkDirectory.IsEmpty())
	{
		const FString Partial = OutputFile + TEXT(".") + FPaths::GetCleanFilename(WorkDirectory) + TEXT(".partial");
		IFileManager::Get().Delete(*Partial, false, true, true);
	}
}

void FModPackageEditor::Finish(const FText& Message, bool bSuccess)
{
	if (TSharedPtr<SNotificationItem> Item = Notification.Pin())
	{
		Item->SetText(Message);
		Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		Item->SetExpireDuration(bSuccess ? 8.0f : 15.0f);
		Item->ExpireAndFadeout();
	}
	Notification.Reset();
}

void FModPackageEditor::OpenOutputDirectory() const
{
	FString Directory = FPaths::GetPath(OutputFile);
	if (Directory.IsEmpty())
	{
		Directory = GetDefault<UModPackagingSettings>()->OutputDirectory.Path;
	}
	if (Directory.IsEmpty())
	{
		Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PackagedMods"));
	}
	Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Directory);
	if (IFileManager::Get().MakeDirectory(*Directory, true))
	{
		FPlatformProcess::ExploreFolder(*Directory);
	}
}
} // namespace Mod::Editor

#undef LOCTEXT_NAMESPACE
