// Copyright (c) 2026 Nelaric Contributors
#include "GenerationModel.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace Mod::HeaderTool
{
namespace
{
using FJson = nlohmann::json;

std::string Required(const FJson& Object, const char* Key)
{
	if (!Object.contains(Key) || !Object[Key].is_string() || Object[Key].get<std::string>().empty() ||
	    Object[Key].get<std::string>().find('\0') != std::string::npos)
	{
		throw std::runtime_error(std::string("Build input requires a nonempty string '") + Key + "'.");
	}
	return Object[Key].get<std::string>();
}

bool Identifier(std::string_view Text)
{
	return !Text.empty() && (std::isalpha(static_cast<unsigned char>(Text[0])) || Text[0] == '_') &&
	       std::all_of(Text.begin(), Text.end(), [](unsigned char C) { return std::isalnum(C) || C == '_'; });
}

void RequireName(const std::string& Text)
{
	if (!Identifier(Text))
	{
		throw std::runtime_error("Expected an ASCII identifier in build input, found '" + Text + "'.");
	}
}

std::string Fold(std::string Text)
{
	std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char C) { return std::tolower(C); });
	return Text;
}

fs::path Resolve(const fs::path& Root, const std::string& Text)
{
	const fs::path Path = PathFromUtf8(Text);
	return fs::canonical(Path.is_absolute() ? Path : Root / Path);
}
} // namespace

FGenerationInput LoadGenerationInput(const fs::path& File)
{
	FGenerationInput Result;
	Result.File = fs::canonical(File);
	const FJson Json = FJson::parse(ReadTextFile(Result.File));
	if (!Json.is_object() || !Json.contains("schemaVersion") || !Json["schemaVersion"].is_number_integer() ||
	    Json.value("schemaVersion", 0) != 1 || Json.value("objectReturnPolicy", "") != "ErrorIfNull")
	{
		throw std::runtime_error("Build input requires schemaVersion 1 and objectReturnPolicy 'ErrorIfNull'.");
	}
	Result.Project = LoadProject(Resolve(Result.File.parent_path(), Required(Json, "project")));
	Result.Database = Resolve(Result.Project.Root, Required(Json, "compilationDatabase"));
	if (Result.Database.filename() != "compile_commands.json")
	{
		throw std::runtime_error("compilationDatabase must name compile_commands.json.");
	}
	Result.GameId = Required(Json, "gameId");
	RequireName(Result.GameId);
	Result.Target = Required(Json, "target");
	Result.Platform = Required(Json, "platform");
	Result.Configuration = Required(Json, "configuration");
	Result.Architecture = Required(Json, "architecture");
	if (Result.Platform != "Win64" || Result.Architecture != "x64")
	{
		throw std::runtime_error("The MHT UObject API backend requires a Win64 x64 compilation database.");
	}
	for (const auto& Name : {Result.Target, Result.Platform, Result.Configuration, Result.Architecture})
	{
		RequireName(Name);
	}
	FJson TypeMap;
	if (Json.contains("typeIdentities"))
	{
		if (Json.contains("types"))
		{
			throw std::runtime_error("Choose types or typeIdentities, not both.");
		}
		Result.IdentityFile = Resolve(Result.Project.Root, Required(Json, "typeIdentities"));
		const auto Identities = FJson::parse(ReadTextFile(Result.IdentityFile));
		if (Identities.value("schemaVersion", 0) != 1)
		{
			throw std::runtime_error("Unsupported type identity schema.");
		}
		TypeMap = Identities.at("types");
	}
	else if (Json.contains("types"))
	{
		TypeMap = Json["types"];
	}
	if (!Json.contains("modules") || !Json["modules"].is_array() || Json["modules"].empty() || !TypeMap.is_object())
	{
		throw std::runtime_error("Build input requires nonempty 'modules' and an explicit 'types' identity map.");
	}
	std::set<std::string> ModuleNames;
	std::set<fs::path> Units;
	for (const auto& Item : Json["modules"])
	{
		FModuleInput Module;
		Module.Name = Required(Item, "name");
		RequireName(Module.Name);
		if (!ModuleNames.insert(Fold(Module.Name)).second)
		{
			throw std::runtime_error("Duplicate module '" + Module.Name + "'.");
		}
		Module.Root = Resolve(Result.Project.Root, Required(Item, "sourceDirectory"));
		if (!fs::is_directory(Module.Root) || !Item.contains("translationUnits") ||
		    !Item["translationUnits"].is_array() || Item["translationUnits"].empty())
		{
			throw std::runtime_error("Module requires a source directory and explicit translationUnits: " +
			                         Module.Name);
		}
		for (const auto& Unit : Item["translationUnits"])
		{
			const fs::path Path = Resolve(Result.Project.Root, Unit.get<std::string>());
			const fs::path Relative = Path.lexically_relative(Module.Root);
			if (Relative.empty() || *Relative.begin() == ".." || !Units.insert(Path).second)
			{
				throw std::runtime_error("Translation unit must belong to exactly one module: " + PathToUtf8(Path));
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
			RequireName(Cpp.substr(Begin, End == Cpp.npos ? Cpp.npos : End - Begin));
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
		FTypeIdentity Type{Cpp, Required(It.value(), "typeId"), Required(It.value(), "scope")};
		if ((Type.Scope != "World" && Type.Scope != "Player") || !Ids.insert(Fold(Type.Id)).second)
		{
			throw std::runtime_error("Type requires a unique TypeId and World/Player scope: " + Cpp);
		}
		Result.Types.emplace(Cpp, std::move(Type));
	}
	if (Json.contains("analysisArguments"))
	{
		Result.AnalysisArguments = Json["analysisArguments"].get<std::vector<std::string>>();
	}
	Result.PathRoots.emplace("Project", Result.Project.Root);
	for (const auto& Module : Result.Modules)
	{
		Result.PathRoots.emplace("Module_" + Module.Name, Module.Root);
	}
	if (Json.contains("pathRoots"))
	{
		for (auto It = Json["pathRoots"].begin(); It != Json["pathRoots"].end(); ++It)
		{
			RequireName(It.key());
			if (!Result.PathRoots.emplace(It.key(), Resolve(Result.Project.Root, It.value().get<std::string>())).second)
			{
				throw std::runtime_error("Duplicate path root: " + It.key());
			}
		}
	}
	return Result;
}

std::string NormalizeInput(const FGenerationInput& Input, std::string Text)
{
	std::replace(Text.begin(), Text.end(), '\\', '/');
	std::vector<std::pair<std::string, std::string>> Roots;
	for (const auto& [Name, Path] : Input.PathRoots)
	{
		Roots.emplace_back(PathToUtf8(Path), "<" + Name + ">");
	}
	std::sort(Roots.begin(), Roots.end(), [](const auto& A, const auto& B) { return A.first.size() > B.first.size(); });
	for (const auto& [Path, Name] : Roots)
	{
		std::size_t Position = 0;
		while ((Position = Text.find(Path, Position)) != Text.npos)
		{
			Text.replace(Position, Path.size(), Name);
			Position += Name.size();
		}
	}
	return Text;
}
} // namespace Mod::HeaderTool
