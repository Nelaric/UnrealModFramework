// Copyright (c) 2026 Nelaric Contributors
/** @file ModPackagingSettings.h */
#pragma once

#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"

#include "ModPackagingSettings.generated.h"

/// Mod packaging options.
UCLASS(Config = EditorPerProjectUserSettings, MinimalAPI, meta = (DisplayName = "Mod 打包"))
class UModPackagingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/// Packages the Runtime modules and content of the open project.
	UPROPERTY(Config, EditAnywhere, Category = "Mod", meta = (DisplayName = "打包当前工程"))
	bool bPackageProject = false;

	/// Selected content plugin.
	UPROPERTY(Config, EditAnywhere, Category = "Mod",
	          meta = (DisplayName = "Mod 内容插件", GetOptions = "GetModPluginNames",
	                  EditCondition = "!bPackageProject"))
	FString PluginName;

	/// Published mod version.
	UPROPERTY(Config, EditAnywhere, Category = "Mod",
	          meta = (DisplayName = "Mod 版本", ToolTip = "三段数字版本，例如 1.0.0。已存在的包不会被覆盖。"))
	FString ModVersion = TEXT("1.0.0");

	/// Target game identity.
	UPROPERTY(Config, EditAnywhere, Category = "Release",
	          meta = (DisplayName = "游戏标识", ToolTip = "填写目标游戏 SDK 提供的稳定游戏标识。"))
	FString GameId;

	/// Base release version.
	UPROPERTY(Config, EditAnywhere, Category = "Release",
	          meta = (DisplayName = "基础发布版本", ToolTip = "目标游戏创建基础发布时使用的版本名。"))
	FString BaseReleaseVersion;

	/// Base release directory.
	UPROPERTY(
	    Config, EditAnywhere, Category = "Release",
	    meta = (DisplayName = "基础发布目录",
	            ToolTip = "包含 <版本>/<Cook 平台>/AssetRegistry.bin 的 Releases 根目录；留空使用项目 Releases 目录。"))
	FDirectoryPath BaseReleaseDirectory;

	/// UBT receipt from the modular host build.
	UPROPERTY(Config, EditAnywhere, Category = "Release",
	          meta = (DisplayName = "宿主 Target 回执", FilePathFilter = "target", EditCondition = "bPackageProject"))
	FFilePath HostTargetReceipt;

	/// Host Engine directory, defaulting to the current Editor engine.
	UPROPERTY(Config, EditAnywhere, Category = "Release",
	          meta = (DisplayName = "宿主 Engine 目录", EditCondition = "bPackageProject",
	                  ToolTip = "选择包含 Binaries、Build 和 Source 的 Engine 目录。留空使用当前编辑器的 Engine。"))
	FDirectoryPath HostEngineDirectory;

	/// Target UAT platform.
	UPROPERTY(Config, EditAnywhere, Category = "Packaging",
	          meta = (DisplayName = "目标平台", GetOptions = "GetTargetPlatforms"))
	FString TargetPlatform = TEXT("Win64");

	/// Native container backend.
	UPROPERTY(Config, EditAnywhere, Category = "Packaging",
	          meta = (DisplayName = "使用 IoStore", ToolTip = "必须与目标游戏的发布配置一致。关闭时生成 Pak 内容。"))
	bool bUseIoStore = true;

	/// Additional engine assets.
	UPROPERTY(Config, EditAnywhere, Category = "Packaging", AdvancedDisplay,
	          meta = (DisplayName = "允许新增引擎内容",
	                  ToolTip = "允许 DLC 烘焙基础发布中不存在的引擎内容；应遵循目标游戏 SDK 的要求。"))
	bool bIncludeEngineContent = false;

	/// Published output folder.
	UPROPERTY(Config, EditAnywhere, Category = "Packaging",
	          meta = (DisplayName = "输出目录", ToolTip = "留空使用项目 Saved/PackagedMods 目录。"))
	FDirectoryPath OutputDirectory;

	/// Build timeout in minutes.
	UPROPERTY(Config, EditAnywhere, Category = "Packaging", AdvancedDisplay,
	          meta = (DisplayName = "超时（分钟）", ClampMin = "1", ClampMax = "1440"))
	int32 TimeoutMinutes = 120;

public:
	/// Resolves the output folder to an absolute filesystem path.
	FString ResolveOutputDirectory() const;

	FName GetContainerName() const override;
	FName GetCategoryName() const override;
	FName GetSectionName() const override;
	FText GetSectionText() const override;

private:
	UFUNCTION()
	TArray<FString> GetModPluginNames() const;

	UFUNCTION()
	TArray<FString> GetTargetPlatforms() const;
};
