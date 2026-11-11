// Copyright (c) 2026 Nelaric Contributors
/** @file ProcessOutput.h */
#pragma once

#include "Common/Process/Handle.h"

#include <filesystem>

namespace Mod::BuildTool
{
/// Routes child stdout to a fresh result file or the existing diagnostics stream.
class ProcessOutput
{
public:
	explicit ProcessOutput(const std::filesystem::path& File);
	ProcessOutput(const ProcessOutput&) = delete;
	ProcessOutput& operator=(const ProcessOutput&) = delete;

#ifdef _WIN32
	HANDLE GetHandle(HANDLE Diagnostics) const;
#else
	bool RedirectInChild() const;
#endif

private:
#ifdef _WIN32
	Handle Output;
#else
	std::filesystem::path Output;
#endif
};
} // namespace Mod::BuildTool
