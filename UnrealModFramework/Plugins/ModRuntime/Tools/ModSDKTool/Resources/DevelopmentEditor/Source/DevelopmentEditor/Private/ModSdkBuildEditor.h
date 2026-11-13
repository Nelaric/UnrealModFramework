// Copyright (c) 2026 Nelaric Contributors
/** @file ModSdkBuildEditor.h */
#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"

namespace Mod::Editor
{
/// Owns one asynchronous SDK tool task; never depends on a game's Runtime.
class FModSdkBuildEditor
{
public:
	/// Removes menus, cancels an active process tree, and releases pipes.
	~FModSdkBuildEditor();
	/// Registers SDK planning, building, and cancellation under Tools.
	void Startup();

private:
	void RegisterMenus();
	void Start(const FString& Command);
	void Cancel();
	bool Tick(float DeltaTime);
	void CloseProcess();
	void Notify(const FText& Message, bool bSuccess) const;
	FString FindExecutable() const;
	bool ChooseFile(const FString& Title, const FString& Filter, FString& OutFile) const;

	FProcHandle Process;
	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	FTSTicker::FDelegateHandle TickHandle;
	FString PendingLog;
	FString ResultFile;
	FString ActiveCommand;
};
} // namespace Mod::Editor
