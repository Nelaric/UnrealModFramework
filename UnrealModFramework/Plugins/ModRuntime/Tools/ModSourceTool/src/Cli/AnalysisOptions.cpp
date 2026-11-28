// Copyright (c) 2026 Nelaric Contributors
#include "AnalysisOptions.h"

#include "Common/Files/File.h"
#include "Common/Files/Path.h"

#include <set>
#include <stdexcept>

namespace Mod::SourceTool
{
namespace
{
std::string Text(const Json& Object, const char* Key)
{
	const auto Value = Object.at(Key).get<std::string>();
	if (Value.empty() || Value.find('\0') != std::string::npos)
	{
		throw std::runtime_error(std::string("Expected a nonempty analysis request field: ") + Key);
	}
	return Value;
}
} // namespace

AnalysisOptions AnalysisOptions::Read(const std::filesystem::path& File)
{
	using Mod::BuildTool::Path;
	const auto Document = Json::parse(Mod::BuildTool::File::ReadFile(File));
	if (Document.value("format", "") != "UMF.SourceAnalysisRequest" || Document.value("schemaVersion", 0) != 2 ||
	    !Document.at("members").is_array() || Document.at("members").empty())
	{
		throw std::runtime_error("Expected a UMF.SourceAnalysisRequest v2 with discovered members.");
	}
	const auto Base = File.parent_path();
	const SourceRequest Shared{
	    .Database = Path::ResolvePath(Base, Text(Document, "compilationDatabase")),
	    .Library = Path::ResolvePath(Base, Text(Document, "libclang")),
	    .ResourceDir = Path::ResolvePath(Base, Text(Document, "resourceDirectory")),
	    .ProjectRoot = Path::ResolvePath(Base, Text(Document, "projectRoot")),
	    .Target = Text(Document, "target"),
	    .AnalysisArguments = Document.at("analysisArguments").get<std::vector<std::string>>(),
	    .bAnalyzeAssembly = true,
	};
	AnalysisOptions Result;
	std::set<std::string> Ids;
	for (const auto& Member : Document.at("members"))
	{
		auto& Request = Result.Requests.emplace_back(Shared);
		Request.MemberId = Text(Member, "id");
		if (!Ids.insert(Request.MemberId).second)
		{
			throw std::runtime_error("Duplicate source analysis member: " + Request.MemberId);
		}
		Request.Source = Path::ResolvePath(Base, Text(Member, "source"));
		Request.TranslationUnit = Path::ResolvePath(Base, Text(Member, "translationUnit"));
		Request.Module = Text(Member, "module");
		Request.Function = Text(Member, "function");
		Request.Signature = Member.value("signature", "");
		Request.DeclarationUsr = Member.value("usr", "");
		if (const auto Failure = Member.find("failureReturn"); Failure != Member.end())
		{
			if (!Failure->is_boolean() && !Failure->is_number_integer() && !Failure->is_null())
			{
				throw std::runtime_error("FailureReturn must be a boolean, integer, or null for " + Request.MemberId);
			}
			Request.FailureReturn = *Failure;
		}
	}
	return Result;
}
} // namespace Mod::SourceTool
