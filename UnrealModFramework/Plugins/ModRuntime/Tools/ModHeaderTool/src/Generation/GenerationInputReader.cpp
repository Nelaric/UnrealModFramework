// Copyright (c) 2026 Nelaric Contributors
#include "GenerationInputReader.h"

#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Files/TextFile.h"
#include "ModHeaderTool/src/Generation/GenerationFields.h"
#include "ModHeaderTool/src/Generation/ModuleInput.h"
#include "ModHeaderTool/src/Generation/TypeIdentity.h"
#include "ModHeaderTool/src/Project/Project.h"
#include "ModHeaderTool/src/Project/ProjectReader.h"

#include <cstddef>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

GenerationInput GenerationInputReader::LoadGenerationInput(const fs::path& File)
{
	GenerationInput Result;
	Result.File = fs::canonical(File);
	const Json Document = Json::parse(TextFile::ReadTextFile(Result.File));
	if (!Document.is_object() || !Document.contains("schemaVersion") ||
	    !Document["schemaVersion"].is_number_integer() || Document.value("schemaVersion", 0) != 1 ||
	    Document.value("objectReturnPolicy", "") != "ErrorIfNull")
	{
		throw std::runtime_error("Build input requires schemaVersion 1 and objectReturnPolicy 'ErrorIfNull'.");
	}
	Result.Project = ProjectReader::LoadProject(
	    GenerationFields::Resolve(Result.File.parent_path(), GenerationFields::Required(Document, "project")));
	Result.Database =
	    GenerationFields::Resolve(Result.Project.Root, GenerationFields::Required(Document, "compilationDatabase"));
	if (Result.Database.filename() != "compile_commands.json")
	{
		throw std::runtime_error("compilationDatabase must name compile_commands.json.");
	}
	Result.GameId = GenerationFields::Required(Document, "gameId");
	GenerationFields::RequireName(Result.GameId);
	Result.Target = GenerationFields::Required(Document, "target");
	Result.Platform = GenerationFields::Required(Document, "platform");
	Result.Configuration = GenerationFields::Required(Document, "configuration");
	Result.Architecture = GenerationFields::Required(Document, "architecture");
	if (Result.Platform != "Win64" || Result.Architecture != "x64")
	{
		throw std::runtime_error("The MHT UObject API backend requires a Win64 x64 compilation database.");
	}
	for (const auto& Name : {Result.Target, Result.Platform, Result.Configuration, Result.Architecture})
	{
		GenerationFields::RequireName(Name);
	}
	Json TypeMap;
	if (Document.contains("typeIdentities"))
	{
		if (Document.contains("types"))
		{
			throw std::runtime_error("Choose types or typeIdentities, not both.");
		}
		Result.IdentityFile =
		    GenerationFields::Resolve(Result.Project.Root, GenerationFields::Required(Document, "typeIdentities"));
		const auto Identities = Json::parse(TextFile::ReadTextFile(Result.IdentityFile));
		if (Identities.value("schemaVersion", 0) != 1)
		{
			throw std::runtime_error("Unsupported type identity schema.");
		}
		TypeMap = Identities.at("types");
	}
	else if (Document.contains("types"))
	{
		TypeMap = Document["types"];
	}
	if (!Document.contains("modules") || !Document["modules"].is_array() || Document["modules"].empty() ||
	    !TypeMap.is_object())
	{
		throw std::runtime_error("Build input requires nonempty 'modules' and an explicit 'types' identity map.");
	}
	std::set<std::string> ModuleNames;
	std::set<fs::path> Units;
	for (const auto& Item : Document["modules"])
	{
		ModuleInput Module;
		Module.Name = GenerationFields::Required(Item, "name");
		GenerationFields::RequireName(Module.Name);
		if (!ModuleNames.insert(GenerationFields::Fold(Module.Name)).second)
		{
			throw std::runtime_error("Duplicate module '" + Module.Name + "'.");
		}
		Module.Root =
		    GenerationFields::Resolve(Result.Project.Root, GenerationFields::Required(Item, "sourceDirectory"));
		if (!fs::is_directory(Module.Root) || !Item.contains("translationUnits") ||
		    !Item["translationUnits"].is_array() || Item["translationUnits"].empty())
		{
			throw std::runtime_error("Module requires a source directory and explicit translationUnits: " +
			                         Module.Name);
		}
		for (const auto& Unit : Item["translationUnits"])
		{
			const fs::path Path = GenerationFields::Resolve(Result.Project.Root, Unit.get<std::string>());
			const fs::path Relative = Path.lexically_relative(Module.Root);
			if (Relative.empty() || *Relative.begin() == ".." || !Units.insert(Path).second)
			{
				throw std::runtime_error("Translation unit must belong to exactly one module: " +
				                         DiagnosticLocation::QuoteDiagnosticPath(Path));
			}
			Module.TranslationUnits.push_back(Path);
		}
		Result.Modules.push_back(std::move(Module));
	}
	std::set<std::string> Ids;
	for (auto It = TypeMap.begin(); It != TypeMap.end(); ++It)
	{
		const std::string Cpp = It.key();
		std::size_t Begin = 0;
		while (Begin < Cpp.size())
		{
			const auto End = Cpp.find("::", Begin);
			GenerationFields::RequireName(Cpp.substr(Begin, End == Cpp.npos ? Cpp.npos : End - Begin));
			if (End == Cpp.npos)
			{
				break;
			}
			Begin = End + 2;
			if (Begin == Cpp.size())
			{
				throw std::runtime_error("Incomplete qualified type name: " + Cpp);
			}
		}
		if (Cpp.empty())
		{
			throw std::runtime_error("Empty C++ type identity.");
		}
		TypeIdentity Type{Cpp, GenerationFields::Required(It.value(), "typeId"),
		                  GenerationFields::Required(It.value(), "scope")};
		if ((Type.Scope != "World" && Type.Scope != "Player") || !Ids.insert(GenerationFields::Fold(Type.Id)).second)
		{
			throw std::runtime_error("Type requires a unique TypeId and World/Player scope: " + Cpp);
		}
		Result.Types.emplace(Cpp, std::move(Type));
	}
	if (Document.contains("analysisArguments"))
	{
		Result.AnalysisArguments = Document["analysisArguments"].get<std::vector<std::string>>();
	}
	Result.PathRoots.emplace("Project", Result.Project.Root);
	for (const auto& Module : Result.Modules)
	{
		Result.PathRoots.emplace("Module_" + Module.Name, Module.Root);
	}
	if (Document.contains("pathRoots"))
	{
		for (auto It = Document["pathRoots"].begin(); It != Document["pathRoots"].end(); ++It)
		{
			GenerationFields::RequireName(It.key());
			if (!Result.PathRoots
			         .emplace(It.key(), GenerationFields::Resolve(Result.Project.Root, It.value().get<std::string>()))
			         .second)
			{
				throw std::runtime_error("Duplicate path root: " + It.key());
			}
		}
	}
	return Result;
}
} // namespace Mod::HeaderTool
