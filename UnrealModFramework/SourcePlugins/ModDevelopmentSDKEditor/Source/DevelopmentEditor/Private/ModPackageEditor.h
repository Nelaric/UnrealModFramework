// Copyright (c) 2026 Nelaric Contributors
/** @file ModPackageEditor.h */
#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"

class SNotificationItem;
class SWidget;
class SWindow;

namespace Mod::Editor
{
/// Owns the Mod toolbar.
class FModPackageEditor
{
public:
	/// Cancels work on shutdown.
	~FModPackageEditor();
	/// Registers the toolbar.
	void Startup();

private:
	void RegisterMenus();
	TSharedRef<SWidget> MakeMenu();
	void OpenPackagingDialog();
	bool StartPackaging();
	bool Tick(float DeltaTime);
	void Cancel();
	void CloseProcess();
	void Finish(const FText& Message, bool bSuccess);
	void OpenOutputDirectory() const;

	FProcHandle Process;
	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	FTSTicker::FDelegateHandle TickHandle;
	TWeakPtr<SWindow> PackagingWindow;
	TWeakPtr<SNotificationItem> Notification;
	FText ValidationError;
	FString PendingLog;
	FString WorkDirectory;
	FString OutputFile;
	double Deadline = 0;
};
} // namespace Mod::Editor
