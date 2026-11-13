// Copyright (c) 2026 Nelaric Contributors
/** @file DevelopmentEditorResources.h */
#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace Mod::SdkTool
{
struct EmbeddedResource
{
	std::string_view RelativePath;
	const unsigned char* Bytes;
	std::size_t Size;
};

/// Returns the files embedded in this build of MDT.
std::span<const EmbeddedResource> GetDevelopmentEditorResources();
} // namespace Mod::SdkTool
