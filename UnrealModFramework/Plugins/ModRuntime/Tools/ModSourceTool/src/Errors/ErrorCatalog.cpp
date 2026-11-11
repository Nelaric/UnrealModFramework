// Copyright (c) 2026 Nelaric Contributors
#include "ErrorCatalog.h"

#include "Common/Errors/ErrorCodes.h"
#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "ModSourceTool/src/Errors/CatalogText.h"
#include "ModSourceTool/src/Errors/ErrorBindings.h"
#include "ModSourceTool/src/Errors/ErrorCodePolicy.h"
#include "ModSourceTool/src/Errors/PendingBindings.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

ErrorCatalog::ErrorCatalog(fs::path CatalogPath, std::string CatalogMemberId, bool bUpdate)
    : Path(std::move(CatalogPath)), MemberId(std::move(CatalogMemberId))
{
	if (bUpdate)
	{
		Lock.emplace(Path, ".modsource-lock");
	}
	Document = Json::parse(Mod::BuildTool::File::ReadFile(Path));
	const auto& Version = Document.at("schemaVersion");
	bStructured = Version == Tools::ErrorCodes::StructuredErrorCodeVersion;
	if (Document.at("format") != "UMF.SourceErrorCatalog" || (!bStructured && Version != 1))
	{
		throw std::runtime_error("Unsupported source error catalog format or version.");
	}
	const auto& Members = Document.at("members");
	ClassifiedReasons = Document.value("classifiedReasons", Json::object());
	const auto& Counters = Document.at(CounterKey());
	if (bStructured)
	{
		ErrorCodePolicy::ValidateStructuredCodes(Members, ClassifiedReasons, Counters);
	}
	else
	{
		ErrorCodePolicy::ValidateLegacyCodes(Members, Counters);
	}
}

SourceRequest ErrorCatalog::MakeAnalysisRequest(std::string_view Database, std::string_view Library,
                                                std::string_view ResourceDirectory, std::string Target,
                                                std::vector<std::string> AnalysisArguments) const
{
	const auto& Member = Document.at("members").at(MemberId);
	std::optional<Json> FailureReturn;
	if (const auto Value = Member.find("failureReturn"); Value != Member.end())
	{
		if (!Value->is_boolean() && !Value->is_number_integer() && !Value->is_null())
		{
			throw std::runtime_error("failureReturn must be boolean, integer, or null.");
		}
		FailureReturn = *Value;
	}
	if (!bStructured && FailureReturn != Json(false))
	{
		throw std::runtime_error("A legacy member requires false as its failure return.");
	}
	return {
	    .Database = Mod::BuildTool::Path::ResolvePath({}, Database),
	    .Library = Mod::BuildTool::Path::ResolvePath({}, Library),
	    .ResourceDir = Mod::BuildTool::Path::ResolvePath({}, ResourceDirectory),
	    .Source = Mod::BuildTool::Path::ResolvePath(Path.parent_path(), CatalogText::Text(Member, "source")),
	    .ProjectRoot = Path.parent_path().parent_path(),
	    .Target = std::move(Target),
	    .Module = CatalogText::Text(Member, "module"),
	    .Function = CatalogText::Text(Member, "function"),
	    .Signature =
	        !bStructured || Member.contains("signature") ? CatalogText::Text(Member, "signature") : std::string{},
	    .AnalysisArguments = std::move(AnalysisArguments),
	    .FailureReturn = std::move(FailureReturn),
	    .bAnalyzeAssembly = bStructured,
	};
}

void ErrorCatalog::BindReport(Json& Report)
{
	bChanged = BindCodes(Report.at("failures")) || bChanged;
	Report["format"] = "UMF.SourceErrorReport";
	Report["schemaVersion"] = 1;
	Report["memberId"] = MemberId;
	Report["catalog"] = Mod::BuildTool::Path::PathToUtf8(Path);
	if (bStructured)
	{
		Report["errorCodeEncodingVersion"] = Tools::ErrorCodes::StructuredErrorCodeVersion;
	}
}

void ErrorCatalog::SaveChanges()
{
	if (!bChanged)
	{
		return;
	}
	if (bStructured)
	{
		ErrorCodePolicy::ValidateStructuredCodes(Document.at("members"), ClassifiedReasons, Document.at(CounterKey()));
		if (!ClassifiedReasons.empty())
		{
			Document["classifiedReasons"] = ClassifiedReasons;
		}
	}
	Mod::BuildTool::File::WriteFileAtomically(Path, Document.dump(2) + '\n');
	bChanged = false;
}

const char* ErrorCatalog::CounterKey() const
{
	return bStructured ? "nextDetailByType" : "nextBusinessCode";
}

bool ErrorCatalog::BindCodes(Json& Failures)
{
	auto& Reasons = Document.at("members").at(MemberId).at("reasons");
	auto& Counters = Document.at(CounterKey());
	const bool bUpdate = Lock.has_value();
	auto Pending = ErrorBindings::MatchReasons(Failures, Reasons, ClassifiedReasons, bStructured, bUpdate);
	for (const auto& [Key, bClassified] : Pending.Unassigned)
	{
		if (!bUpdate)
		{
			throw std::runtime_error("Unassigned error key " + Key + ". Run the update-catalog command.");
		}
		auto& Reason = (bClassified ? ClassifiedReasons : Reasons).at(Key);
		Reason["code"] = ErrorCodePolicy::AllocateCode(Counters, Reason, Key, bStructured);
	}
	ErrorBindings::ApplyAssignedCodes(Pending.Failures, Reasons, ClassifiedReasons, bStructured);
	Failures = std::move(Pending.Failures);
	return !Pending.Unassigned.empty();
}
} // namespace Mod::SourceTool
