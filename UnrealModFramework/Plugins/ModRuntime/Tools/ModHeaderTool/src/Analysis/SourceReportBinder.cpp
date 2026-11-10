// Copyright (c) 2026 Nelaric Contributors
#include "SourceReportBinder.h"

#include "Common/Errors/ErrorCodes.h"
#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Generation/GenerationInput.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <system_error>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

bool SourceReportBinder::SameFile(const Json& Report, const char* Key, const fs::path& File)
{
	std::error_code Error;
	return fs::equivalent(Path::PathFromUtf8(Report.at(Key).get<std::string>()), File, Error);
}

void SourceReportBinder::BindSourceErrors(API& API, GenerationModel& Model, const Json& Member,
                                          const Json& ClassifiedReasons, const Json& Report, const fs::path& Database)
{
	const auto& Definition = Report.at("definition");
	const auto Function = Definition.at("function").get<std::string>();
	const auto Signature = Definition.at("signature").get<std::string>();
	const auto Source =
	    Model.SourceErrorCatalog.parent_path() / Path::PathFromUtf8(Member.at("source").get<std::string>());
	if (Report.value("format", "") != "UMF.SourceErrorReport" || Report.value("schemaVersion", 0) != 1 ||
	    Report.value("errorCodeEncodingVersion", 1) != Model.ErrorCodeEncodingVersion ||
	    Report.at("memberId") != API.Id || Report.at("target") != Model.Input.Target ||
	    Report.at("module") != API.Module || API.DeclarationUsr.empty() || Definition.at("usr") != API.DeclarationUsr ||
	    Function + " : " + Signature != API.SourceSignature || !SameFile(Definition.at("location"), "file", Source) ||
	    !SameFile(Report, "catalog", Model.SourceErrorCatalog) || !SameFile(Report, "compilationDatabase", Database))
	{
		throw std::runtime_error("ModSourceTool report does not match the bound export and Target: " + API.Id);
	}
	const auto& Failures = Report.at("failures");
	if (!Failures.is_array())
	{
		throw std::runtime_error("ModSourceTool report requires a failures array for " + API.Id);
	}
	for (const auto& File : Report.value("analyzedFiles", Json::array()))
	{
		const auto Path = Path::PathFromUtf8(File.get<std::string>());
		if (!Path.is_absolute() || !fs::is_regular_file(Path))
		{
			throw std::runtime_error("Invalid source analysis dependency for " + API.Id + ": " +
			                         DiagnosticLocation::QuoteDiagnosticPath(Path));
		}
		Model.Dependencies.insert(fs::canonical(Path));
	}
	auto& Codes = API.SourceErrorCodes.emplace();
	for (const auto& Failure : Failures)
	{
		const auto Key = Failure.at("key").get<std::string>();
		const auto& Value = Failure.at("code");
		const auto& Reasons = Failure.contains("classification") ? ClassifiedReasons : Member.at("reasons");
		const auto& Reason = Reasons.at(Key);
		if (!Value.is_number_integer() || Value.get<std::uint64_t>() == 0 ||
		    Value.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max() || Value != Reason.at("code"))
		{
			throw std::runtime_error("ModSourceTool returned an invalid catalog code for " + API.Id + '/' + Key);
		}
		const auto Code = Value.get<std::uint32_t>();
		Codes.emplace(Key, Code);
		if (Model.ErrorCodeEncodingVersion == Tools::ErrorCodes::StructuredErrorCodeVersion)
		{
			const auto Macro = Failure.at("macro").get<std::string>();
			if (!Tools::ErrorCodes::IsErrorMacro(Macro) || Reason.at("macro") != Macro)
			{
				throw std::runtime_error("ModSourceTool returned an invalid macro for " + API.Id + '/' + Key);
			}
			const auto [Entry, bInserted] = Model.SourceErrorMacros.emplace(Macro, Code);
			if (!bInserted && Entry->second != Code)
			{
				throw std::runtime_error("Conflicting source error macro: " + Macro);
			}
		}
	}
}
} // namespace Mod::HeaderTool
