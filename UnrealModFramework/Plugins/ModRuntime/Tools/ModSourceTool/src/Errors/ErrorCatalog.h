// Copyright (c) 2026 Nelaric Contributors
/** @file ErrorCatalog.h */
#pragma once

#include "Common/Files/BuildLock.h"
#include "ModSourceTool/src/Analysis/SourceRequest.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

/// Binds analysis findings to the stable error codes for one catalog member.
class ErrorCatalog
{
public:
	/// Validates the catalog and holds its writer lock for update requests.
	ErrorCatalog(fs::path CatalogPath, std::string CatalogMemberId, bool bUpdate);

	/// Combines compiler inputs with the configured member's analysis policy.
	SourceRequest MakeAnalysisRequest(std::string_view Database, std::string_view Library,
	                                  std::string_view ResourceDirectory, std::string Target,
	                                  std::vector<std::string> AnalysisArguments) const;
	/// Adds stable error codes and catalog identity to an analysis report.
	void BindReport(Json& Report);
	/// Publishes assigned codes after successful report preparation.
	void SaveChanges();

private:
	const char* CounterKey() const;
	bool BindCodes(Json& Failures);

	fs::path Path;
	std::string MemberId;
	std::optional<Mod::BuildTool::BuildLock> Lock;
	Json Document;
	Json ClassifiedReasons;
	bool bStructured = false;
	bool bChanged = false;
};
} // namespace Mod::SourceTool
