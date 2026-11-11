// Copyright (c) 2026 Nelaric Contributors
/** @file ModDirectoryWatcher.h */
#pragma once

#include "Containers/Set.h"
#include "Containers/UnrealString.h"
#include "Templates/UniquePtr.h"

namespace Mod::IO
{
/// Changes collected from a physical directory since the last read.
struct FDirectoryChanges
{
	/// Names of changed entries relative to the watched directory.
	TSet<FString> Names;
	/// A scan is needed because the OS could not report all names.
	bool bRescan = false;
};

/// Nonblocking watch for direct children of one physical directory.
class HAL_API FDirectoryWatcher final
{
public:
	/// Constructs an inactive watch on the calling thread.
	FDirectoryWatcher();

	/// Releases the native watch and any pending notifications.
	~FDirectoryWatcher();

	/** @brief Starts a watch, replacing any previous registration.
	 * @param Directory Absolute path of an existing
	 * directory.
	 * @return Whether the native watch started.
	 * @details Start, read, and stop from the same
	 * thread.
	 */
	bool Start(const FString& Directory);

	/// Releases the active registration.
	void Stop();

	/** @brief Takes the available notifications without waiting.
	 * @return Entry names and whether enumeration is
	 * required.
	 */
	FDirectoryChanges ReadChanges();

	/** @brief Checks whether the native registration is active.
	 * @return Whether the watch can receive
	 * notifications.
	 */
	bool IsActive() const;

private:
	struct FNativeWatch;
	TUniquePtr<FNativeWatch> Native;
};
} // namespace Mod::IO
