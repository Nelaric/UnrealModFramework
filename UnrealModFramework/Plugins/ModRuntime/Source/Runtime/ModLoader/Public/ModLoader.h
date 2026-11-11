// Copyright (c) 2026 Nelaric Contributors
/** @file ModLoader.h */
#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Delegates/Delegate.h"
#include "Misc/Optional.h"
#include "Modules/ModuleInterface.h"
#include "UObject/NameTypes.h"

namespace Mod::Loader
{
/// Cooked container types admitted by the host release.
enum class EBackend : uint8
{
	/// File-based cooked packages inside Pak.
	Pak,
	/// Pak with its native IoStore containers.
	IoStore,
};

/// Identifies the entry point that finished a load.
enum class ELoadOrigin : uint8
{
	/// The configured scan at game or PIE startup.
	Startup,
	/// An explicit LoadPackage or LoadDirectory call.
	Manual,
	/// A package found by the mod directory watch.
	DirectoryWatch,
};

/// Stable results for package preparation and mounting.
enum class ELoadError : uint8
{
	/// Content is mounted.
	None,
	/// The call did not run on the game thread.
	WrongThread,
	/// A load is already in progress.
	Busy,
	/// The host release policy is incomplete or invalid.
	InvalidPolicy,
	/// The installed package directory or archive could not be read.
	InputUnavailable,
	/// The archive, manifest, or index is malformed or unsupported.
	InvalidArchive,
	/// The package does not match the host release or supported features.
	Incompatible,
	/// The allowlist rejected this archive's SHA-256.
	Untrusted,
	/// The package set exceeds a loader limit.
	LimitExceeded,
	/// The preparation time budget was exhausted.
	TimedOut,
	/// A payload does not match its declared digest.
	IntegrityFailure,
	/// The verified cache could not be prepared.
	CacheFailure,
	/// A mod identity, package root, or container collides.
	Conflict,
	/// The host does not expose the required container backend.
	BackendUnavailable,
	/// Native container metadata is invalid or outside the mod namespace.
	InvalidContainer,
	/// The engine could not make the contents available.
	MountFailed,
	/// Engine mount state requires a process restart.
	RestartRequired,
};

/// Release identity and trust settings supplied by the game.
struct FLoadPolicy
{
	/// Stable game identifier from the SDK release.
	FString GameId;
	/// Exact base release name passed to the DLC cooker.
	FString BaseReleaseVersion;
	/// SHA-256 of the base release's AssetRegistry.bin.
	FString BaseRegistrySha256;
	/// Container backend used by the base release.
	EBackend Backend = EBackend::IoStore;
	/// Exact SHA-256 values of complete packages approved by the host.
	TArray<FString> TrustedPackageSha256;
	/// Time budget in seconds for archive and cache preparation.
	float PreparationTimeoutSeconds = 120.0f;
	/// Overrides the default of enforcing only a populated allowlist.
	TOptional<bool> bEnforcePackageAllowlist;
};

/// Content described by a mounted mod package.
struct FMountedPackage
{
	/// Lowercase mod identifier.
	FString ModId;
	/// Published mod version.
	FString Version;
	/// The package namespace fixed at cook time.
	FString PackageRoot;
	/// SHA-256 of the complete archive.
	FString ArchiveSha256;
	/// Provenance identifier written by the cooker.
	FString CookProfileId;
	/// Native container backend.
	EBackend Backend = EBackend::Pak;
	/// Sorted cooked package names in this namespace.
	TArray<FName> PackageNames;
	/// Cooked Blueprint class paths for the mod's session entry points.
	TArray<FString> LifecycleClasses;
};

/// Outcome for one input path, including its mounted content on success.
struct FLoadResult
{
	/// Archive path, or the directory for a scan-level failure.
	FString Path;
	/// Machine-readable failure category.
	ELoadError Error = ELoadError::None;
	/// Explanation for logs or user interfaces.
	FString Detail;
	/// Mounted content for a successful operation.
	FMountedPackage Package;
	/// True when this exact archive was already mounted.
	bool bAlreadyMounted = false;

	/// Whether the requested content is available to the package loader.
	bool IsSuccess() const
	{
		return Error == ELoadError::None;
	}
};

/** @brief Loads approved cooked content in a game or editor PIE session.
 * @details Use from a game-thread startup
 * gate. Loading performs synchronous
 * I/O and engine mounts. Removing a watched source starts the unload
 * of its
 * package. PIE also releases mounts at the end of play.
 */
class MODLOADER_API IModLoaderModule : public IModuleInterface
{
public:
	/// Receives the origin, input path, and results of a finished load.
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnLoadCompleted, ELoadOrigin, const FString&, TConstArrayView<FLoadResult>);

	/// Receives a mounted package while its content is still available.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnBeforeUnload, const FMountedPackage&);

	/** @brief Returns the service owned by the enabled ModLoader module.
	 * @return The service, valid until module
	 * shutdown.
	 */
	static IModLoaderModule& Get();

	/** @brief Verifies and mounts one archive on the game thread.
	 * @param Path Path to the .mod file on the physical
	 * filesystem.
	 * @param Policy Trusted release policy for the current game.
	 * @return Mounted content or a
	 * recoverable failure.
	 */
	virtual FLoadResult LoadPackage(const FString& Path, const FLoadPolicy& Policy) = 0;

	/** @brief Mounts eligible .mod files directly inside a directory.
	 * @param Directory Directory on the physical
	 * filesystem.
	 * @param Policy Trusted release policy for the current game.
	 * @return One result per candidate,
	 * or a scan-level failure.
	 * @details Call on the game thread. Different archives with the same mod
	 * ID in
	 * one scan are rejected before any version of that mod is mounted.
	 */
	virtual TArray<FLoadResult> LoadDirectory(const FString& Directory, const FLoadPolicy& Policy) = 0;

	/** @brief Copies mounted packages that can be activated.
	 * @return A snapshot sorted by mod ID.
	 * @details
	 * Call on the game thread.
	 */
	virtual TArray<FMountedPackage> GetMountedPackages() const = 0;

	/** @brief Gets the startup and manual completion event.
	 * @return The event. Add and remove listeners on the
	 * game thread.
	 * @details One broadcast per operation. A directory result has one entry per
	 * candidate,
	 * one scan error, or no entries if empty. Callback references
	 * are temporary. Reentrant and wrong-thread
	 * completions are deferred.
	 */
	FOnLoadCompleted& OnLoadCompleted()
	{
		return LoadCompleted;
	}

	/** @brief Gets the game-thread event for a package beginning unload.
	 * @return The event. The package reference
	 * is valid during the callback.
	 */
	FOnBeforeUnload& OnBeforeUnload()
	{
		return BeforeUnload;
	}

protected:
	void BroadcastLoadCompleted(ELoadOrigin Origin, const FString& Path, TConstArrayView<FLoadResult> Results)
	{
		LoadCompleted.Broadcast(Origin, Path, Results);
	}

	void BroadcastBeforeUnload(const FMountedPackage& Package)
	{
		BeforeUnload.Broadcast(Package);
	}

private:
	FOnLoadCompleted LoadCompleted;
	FOnBeforeUnload BeforeUnload;
};
} // namespace Mod::Loader
