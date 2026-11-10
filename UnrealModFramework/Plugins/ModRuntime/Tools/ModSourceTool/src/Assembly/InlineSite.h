// Copyright (c) 2026 Nelaric Contributors
/** @file InlineSite.h */
#pragma once

namespace Mod::SourceTool
{
struct InlineSite
{
	unsigned Parent = 0;
	unsigned CallerFile = 0;
	unsigned CallerLine = 0;
	unsigned OriginFile = 0;
	unsigned OriginLine = 0;
};
} // namespace Mod::SourceTool
