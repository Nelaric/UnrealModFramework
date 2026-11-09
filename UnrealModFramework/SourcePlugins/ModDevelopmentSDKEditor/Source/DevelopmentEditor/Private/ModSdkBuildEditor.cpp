// Copyright (c) 2026 Nelaric Contributors
#include "ModSdkBuildEditor.h"

#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "IDesktopPlatform.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "ModSdkBuildEditor"

DEFINE_LOG_CATEGORY_STATIC(LogModSdkBuild, Log, All);

namespace Mod::Editor
{
namespace
{
FString QuotedPath(const FString& Path)
{
	if (Path.Contains(TEXT("\"")) || Path.Contains(TEXT("\r")) || Path.Contains(TEXT("\n")))
	{
		return FString();
	}
	return FString::Printf(TEXT("\"%s\""), *Path);
}
} // namespace

FModSdkBuildEditor::~FModSdkBuildEditor()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}
	if (Process.IsValid())
	{
		FPlatformProcess::TerminateProc(Process, true);
		CloseProcess();
	}
}

void FModSdkBuildEditor::Startup()
{
	if (!IsRunningCommandlet())
	{
		UToolMenus::RegisterStartupCallback(
		    FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FModSdkBuildEditor::RegisterMenus));
	}
}

void FModSdkBuildEditor::RegisterMenus()
{
	FToolMenuOwnerScoped Owner(this);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("ModSDK"), LOCTEXT("Section", "Mod SDK"));
	Section.AddMenuEntry(TEXT("PlanModSDK"), LOCTEXT("Plan", "Plan Mod SDK..."),
	                     LOCTEXT("PlanTip", "Validate an SDK request and inspect its build plan."),
	                     FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Search")),
	                     FUIAction(FExecuteAction::CreateLambda([this] { Start(TEXT("plan")); }),
	                               FCanExecuteAction::CreateLambda([this] { return !Process.IsValid(); })));
	Section.AddMenuEntry(TEXT("BuildModSDK"), LOCTEXT("Build", "Build Mod SDK..."),
	                     LOCTEXT("BuildTip", "Build the declared SDK and independent development plugin."),
	                     FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Build")),
	                     FUIAction(FExecuteAction::CreateLambda([this] { Start(TEXT("build")); }),
	                               FCanExecuteAction::CreateLambda([this] { return !Process.IsValid(); })));
	Section.AddMenuEntry(TEXT("CancelModSDK"), LOCTEXT("Cancel", "Cancel Mod SDK task"),
	                     LOCTEXT("CancelTip", "Stop the active SDK tool and its child processes."), FSlateIcon(),
	                     FUIAction(FExecuteAction::CreateRaw(this, &FModSdkBuildEditor::Cancel),
	                               FCanExecuteAction::CreateLambda([this] { return Process.IsValid(); })));
}

bool FModSdkBuildEditor::ChooseFile(const FString& Title, const FString& Filter, FString& OutFile) const
{
	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (!Desktop)
	{
		return false;
	}
	TArray<FString> Files;
	if (!Desktop->OpenFileDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr), Title,
	                             FPaths::ProjectDir(), FString(), Filter, EFileDialogFlags::None, Files) ||
	    Files.Num() != 1)
	{
		return false;
	}
	OutFile = FPaths::ConvertRelativePathToFull(Files[0]);
	return true;
}

FString FModSdkBuildEditor::FindExecutable() const
{
	FString Configured;
	GConfig->GetString(TEXT("ModSDKTool"), TEXT("Executable"), Configured, GEditorPerProjectIni);
	if (FPaths::FileExists(Configured))
	{
		return FPaths::ConvertRelativePathToFull(Configured);
	}
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ModDevelopmentSDKEditor"));
	if (!Plugin)
	{
		return FString();
	}
#if PLATFORM_WINDOWS
	const FString Relative = TEXT("Binaries/ThirdParty/ModSDKTool/Win64/ModSDKTool.exe");
#elif PLATFORM_MAC
	const FString Relative = TEXT("Binaries/ThirdParty/ModSDKTool/Mac/ModSDKTool");
#else
	const FString Relative = TEXT("Binaries/ThirdParty/ModSDKTool/Linux/ModSDKTool");
#endif
	const FString Packaged = FPaths::Combine(Plugin->GetBaseDir(), Relative);
	return FPaths::FileExists(Packaged) ? FPaths::ConvertRelativePathToFull(Packaged) : FString();
}

void FModSdkBuildEditor::Start(const FString& Command)
{
	if (Process.IsValid())
	{
		return;
	}
	FString Request;
	if (!ChooseFile(LOCTEXT("ChooseRequest", "Choose an SDK build request").ToString(),
	                TEXT("SDK request (*.json)|*.json"), Request))
	{
		return;
	}
	FString Executable = FindExecutable();
	if (Executable.IsEmpty())
	{
		if (!ChooseFile(LOCTEXT("ChooseTool", "Locate ModSDKTool").ToString(),
#if PLATFORM_WINDOWS
		                TEXT("ModSDKTool (*.exe)|*.exe"),
#else
		                TEXT("ModSDKTool|*"),
#endif
		                Executable))
		{
			return;
		}
		GConfig->SetString(TEXT("ModSDKTool"), TEXT("Executable"), *Executable, GEditorPerProjectIni);
		GConfig->Flush(false, GEditorPerProjectIni);
	}
	const FString Results = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ModSDKTool"));
	IFileManager::Get().MakeDirectory(*Results, true);
	ResultFile = FPaths::ConvertRelativePathToFull(
	    FPaths::Combine(Results, FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json")));
	const FString RequestArgument = QuotedPath(Request);
	const FString ResultArgument = QuotedPath(ResultFile);
	if (RequestArgument.IsEmpty() || ResultArgument.IsEmpty() || !FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
	{
		Notify(LOCTEXT("PipeFailed", "Could not prepare the SDK tool task."), false);
		return;
	}
	const FString Arguments =
	    FString::Printf(TEXT("%s --request %s --output %s"), *Command, *RequestArgument, *ResultArgument);
	const FString WorkingDirectory = FPaths::GetPath(Request);
	Process = FPlatformProcess::CreateProc(*Executable, *Arguments, true, true, true, nullptr, 0, *WorkingDirectory,
	                                       WritePipe, nullptr, WritePipe);
	if (!Process.IsValid())
	{
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		ReadPipe = nullptr;
		WritePipe = nullptr;
		Notify(LOCTEXT("LaunchFailed", "Could not start ModSDKTool. See the Output Log."), false);
		return;
	}
	ActiveCommand = Command;
	PendingLog.Reset();
	TickHandle =
	    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FModSdkBuildEditor::Tick), 0.1f);
	UE_LOG(LogModSdkBuild, Display, TEXT("Started SDK %s. Result: %s"), *Command, *ResultFile);
}

bool FModSdkBuildEditor::Tick(float DeltaTime)
{
	(void)DeltaTime;
	PendingLog += FPlatformProcess::ReadPipe(ReadPipe);
	FString Line;
	while (PendingLog.Split(TEXT("\n"), &Line, &PendingLog))
	{
		Line.TrimEndInline();
		UE_LOG(LogModSdkBuild, Display, TEXT("%s"), *Line);
	}
	if (PendingLog.Len() > 32768)
	{
		UE_LOG(LogModSdkBuild, Display, TEXT("%s"), *PendingLog);
		PendingLog.Reset();
	}
	if (FPlatformProcess::IsProcRunning(Process))
	{
		return true;
	}
	PendingLog += FPlatformProcess::ReadPipe(ReadPipe);
	if (!PendingLog.IsEmpty())
	{
		UE_LOG(LogModSdkBuild, Display, TEXT("%s"), *PendingLog);
	}
	int32 ExitCode = 1;
	FPlatformProcess::GetProcReturnCode(Process, &ExitCode);
	UE_LOG(LogModSdkBuild, Display, TEXT("SDK %s finished (%d). Result: %s"), *ActiveCommand, ExitCode, *ResultFile);
	CloseProcess();
	TickHandle.Reset();
	Notify(ExitCode == 0 ? LOCTEXT("Completed", "SDK task completed. Read its result for supported capabilities.")
	                     : LOCTEXT("Failed", "SDK task failed. See the Output Log."),
	       ExitCode == 0);
	return false;
}

void FModSdkBuildEditor::Cancel()
{
	if (Process.IsValid())
	{
		if (!FPlatformProcess::IsProcRunning(Process))
		{
			Tick(0.0f);
			return;
		}
		FPlatformProcess::TerminateProc(Process, true);
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
		CloseProcess();
		Notify(LOCTEXT("Cancelled", "SDK task cancelled. Check its result before using published artifacts."), false);
	}
}

void FModSdkBuildEditor::CloseProcess()
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
}

void FModSdkBuildEditor::Notify(const FText& Message, bool bSuccess) const
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 6.0f;
	const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
	if (Item.IsValid())
	{
		Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}
} // namespace Mod::Editor

#undef LOCTEXT_NAMESPACE
