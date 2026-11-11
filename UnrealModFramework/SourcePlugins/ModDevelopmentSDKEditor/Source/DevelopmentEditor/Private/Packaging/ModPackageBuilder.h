// Copyright (c) 2026 Nelaric Contributors
/** @file ModPackageBuilder.h */
#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UModPackagingSettings;

namespace Mod::Editor
{
/// Inputs for one build.
struct FModPackageRequest
{
	FString PluginName;
	FString ModVersion;
	FString GameId;
	FString BaseReleaseVersion;
	FString BaseReleaseDirectory;
	FString TargetPlatform;
	FString OutputFile;
	FString HostTargetReceipt;
	FString HostEngineDirectory;
	bool bPackageProject = false;
	bool bUseIoStore = true;
	bool bIncludeEngineContent = false;
	int32 TimeoutMinutes = 120;

	/** @brief Reads and validates editor options.
	 * @param Settings The project options.
	 * @param OutRequest The resolved build inputs.
	 * @param OutError The failure explanation.
	 * @return Whether the inputs are valid.
	 */
	static bool FromSettings(const UModPackagingSettings& Settings, FModPackageRequest& OutRequest, FText& OutError);
	/** @brief Reads a versioned build request.
	 * @param File The request JSON path.
	 * @param OutRequest The resolved inputs.
	 * @param OutError The failure explanation.
	 * @return Whether the request is valid.
	 */
	static bool Read(const FString& File, FModPackageRequest& OutRequest, FText& OutError);
	/// Returns the wire request.
	TSharedRef<FJsonObject> ToJson() const;
	/** @brief Checks the selected project or plugin and release.
	 * @param OutError The failure explanation.
	 * @return Whether cooking can begin.
	 */
	bool Validate(FText& OutError) const;
	/// Returns the Cook platform.
	FString GetCookPlatform() const;
};

/// Cooks Mod content.
class FModPackageBuilder
{
public:
	/** @brief Runs UAT and commits an immutable package.
	 * @param Request The validated inputs.
	 * @param WorkDirectory This task's private output directory.
	 * @param OutResult The machine-readable build result.
	 * @return Whether the package was committed.
	 */
	static bool Build(const FModPackageRequest& Request, const FString& WorkDirectory,
	                  TSharedRef<FJsonObject> OutResult);
};
} // namespace Mod::Editor
