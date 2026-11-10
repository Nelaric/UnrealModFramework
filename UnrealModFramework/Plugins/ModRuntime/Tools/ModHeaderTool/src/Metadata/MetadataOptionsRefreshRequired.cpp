// Copyright (c) 2026 Nelaric Contributors
#include "MetadataOptionsRefreshRequired.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace Mod::HeaderTool
{
MetadataOptionsRefreshRequired::MetadataOptionsRefreshRequired(std::string InReason, const char* Hint)
    : std::runtime_error(InReason + Hint), Reason(std::move(InReason))
{
}
} // namespace Mod::HeaderTool
