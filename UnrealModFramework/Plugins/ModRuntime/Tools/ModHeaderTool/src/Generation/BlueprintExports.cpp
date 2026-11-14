// Copyright (c) 2026 Nelaric Contributors
#include "BlueprintExports.h"

#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Files/TextFile.h"
#include "ModHeaderTool/src/Generation/ExportNameRules.h"
#include "ModHeaderTool/src/Generation/GenerationFields.h"
#include "ModHeaderTool/src/Hash/Hash.h"
#include "ModHeaderTool/src/Metadata/MetadataName.h"
#include "ModHeaderTool/src/Metadata/Validator.h"

#include <algorithm>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace Mod::HeaderTool::BlueprintExports
{
namespace
{
using Json = nlohmann::json;

APIType Scalar(const std::string& Property)
{
	static const std::map<std::string, std::string> Types{
	    {"BoolProperty", "bool"},   {"ByteProperty", "uint8"},  {"IntProperty", "int32"},
	    {"Int64Property", "int64"}, {"FloatProperty", "float"}, {"DoubleProperty", "double"},
	};
	const auto It = Types.find(Property);
	if (It == Types.end())
	{
		throw std::runtime_error("Unsupported Blueprint property: " + Property);
	}
	APIType Type;
	Type.Kind = APIType::EKind::Scalar;
	Type.Cpp = Type.Public = It->second;
	return Type;
}

bool IsGuid(const std::string& Text)
{
	return Text.size() == 32 && Text != std::string(32, '0') &&
	       Text.find_first_not_of("0123456789abcdef") == std::string::npos;
}

API ReadAPI(const Json& Record, const ResolvedType& Type, const std::string& Source, const fs::path& Asset,
            std::size_t Index, std::set<std::string>& Guids)
{
	API Result;
	Result.Module = Type.Module;
	Result.Owner = Type.Identity.CppName;
	Result.Name = GenerationFields::Required(Record, "function");
	Result.ExportName = GenerationFields::Required(Record, "name");
	Result.Version = GenerationFields::Required(Record, "version");
	Result.Scope = Type.Identity.Scope;
	Result.NameLocation = Source + ":" + Result.Name;
	Result.Macro.File = Asset;
	Result.Return.Cpp = Result.Return.Public = "void";
	Result.Permissions = Record.at("permissions").get<std::vector<std::string>>();
	std::sort(Result.Permissions.begin(), Result.Permissions.end());
	const auto Guid = ExportNameRules::Fold(GenerationFields::Required(Record, "guid"));
	if (!Record.at("parameters").is_array() || !IsGuid(Guid) || !Guids.insert(Guid).second ||
	    !Validator::IsVersion(Result.Version) || !MetadataName::IsCompatibleFunctionName(Result.ExportName) ||
	    Result.ExportName == "Binding" || Result.ExportName == "FModRuntimeObject" ||
	    !Result.Name.starts_with("ModAPI_"))
	{
		throw std::runtime_error(Result.NameLocation + ": invalid Blueprint API identity or public name.");
	}
	Result.Id = "api/" + Type.Identity.Id + "/" + Guid;
	Result.Symbol = "A_" + Hash::HashBytes(Result.Id).substr(0, 16);
	Result.Blueprint.emplace();
	Result.Blueprint->Index = Index;
	std::set<std::string> ParameterNames;
	for (const auto& Value : Record.at("parameters"))
	{
		BlueprintParameter Parameter{GenerationFields::Required(Value, "name"),
		                             GenerationFields::Required(Value, "propertyClass"),
		                             GenerationFields::Required(Value, "role")};
		if (!ParameterNames.insert(ExportNameRules::Fold(Parameter.Name)).second ||
		    Result.Return.Kind != APIType::EKind::Void)
		{
			throw std::runtime_error(Result.NameLocation + ": unexpected Blueprint parameter list.");
		}
		auto ParameterType = Scalar(Parameter.PropertyClass);
		if (Parameter.Role == "Input")
		{
			Result.Parameters.push_back({"Arg" + std::to_string(Result.Parameters.size()), std::move(ParameterType)});
		}
		else if (Parameter.Role == "Output" || Parameter.Role == "Return")
		{
			Result.Return = std::move(ParameterType);
		}
		else
		{
			throw std::runtime_error(Result.NameLocation + ": unexpected Blueprint parameter direction.");
		}
		Result.Blueprint->Parameters.push_back(std::move(Parameter));
	}
	Json ParameterTypes = Json::array();
	for (const auto& Parameter : Result.Parameters)
	{
		ParameterTypes.push_back(Parameter.Type.Public);
	}
	Result.SourceSignature =
	    Json{{"kind", "Blueprint"}, {"inputs", ParameterTypes}, {"result", Result.Return.Public}}.dump();
	return Result;
}
} // namespace

void Merge(GenerationModel& Model, const fs::path& Snapshot)
{
	if (Snapshot.empty())
	{
		return;
	}
	const auto File = fs::canonical(Snapshot);
	const auto Document = Json::parse(TextFile::ReadTextFile(File));
	if (Document.value("format", "") != "UMF.BlueprintExports" || Document.value("schemaVersion", 0) != 1 ||
	    !Document.at("types").is_array() || !Document.at("inputs").is_array())
	{
		throw std::runtime_error("Expected a UMF.BlueprintExports v1 snapshot.");
	}
	Model.Dependencies.insert(File);
	for (const auto& Input : Document.at("inputs"))
	{
		Model.Dependencies.insert(fs::canonical(Path::PathFromUtf8(Input.get<std::string>())));
	}
	std::set<std::string> Ids, TypeIds, ClassPaths, Guids;
	for (const auto& API : Model.APIs)
	{
		Ids.insert(API.Id);
	}
	for (const auto& Entry : Model.Types)
	{
		TypeIds.insert(ExportNameRules::Fold(Entry.second.Identity.Id));
	}
	for (const auto& Record : Document.at("types"))
	{
		const auto Source = GenerationFields::Required(Record, "asset");
		ResolvedType Type;
		Type.Identity.Id = ExportNameRules::Fold(GenerationFields::Required(Record, "typeId"));
		Type.Identity.Scope = "World";
		Type.Module = GenerationFields::Required(Record, "module");
		Type.BlueprintClass = GenerationFields::Required(Record, "class");
		if (!TypeIds.insert(Type.Identity.Id).second || !ClassPaths.insert(Type.BlueprintClass).second ||
		    Type.Identity.Id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_.-") != std::string::npos ||
		    !Type.BlueprintClass.starts_with("/") || !Type.BlueprintClass.ends_with("_C") ||
		    std::none_of(Model.Input.Modules.begin(), Model.Input.Modules.end(),
		                 [&](const auto& Module) { return Module.Name == Type.Module; }))
		{
			throw std::runtime_error(Source + ": invalid or duplicate Blueprint type, class or host module.");
		}
		const auto Asset = fs::canonical(Path::PathFromUtf8(GenerationFields::Required(Record, "file")));
		Model.Dependencies.insert(Asset);
		Type.Symbol = "T_" + Hash::HashBytes(Type.Identity.Id).substr(0, 16);
		Type.Identity.CppName = "Mod::Generated::FBlueprint_" + Type.Symbol;
		if (!Record.at("apis").is_array() || (Record.contains("extensions") && !Record.at("extensions").is_array()))
		{
			throw std::runtime_error(Source + ": expected arrays of Blueprint declarations.");
		}
		for (const auto& Item : Record.value("extensions", Json::array()))
		{
			Extension Entry;
			const auto Guid = ExportNameRules::Fold(GenerationFields::Required(Item, "guid"));
			Entry.bHook = Item.at("hook").get<bool>();
			Entry.Id = std::string(Entry.bHook ? "hook/" : "event/") + Guid;
			Entry.Module = Type.Module;
			Entry.BlueprintClass = Type.BlueprintClass;
			Entry.Name = GenerationFields::Required(Item, "name");
			Entry.Version = GenerationFields::Required(Item, "version");
			Entry.Permission = Item.at("permission").get<std::string>();
			Entry.Location = Source + ":" + Entry.Name;
			Entry.Macro.File = Asset;
			Entry.bAuthorityOnly = Item.at("authorityOnly").get<bool>();
			Entry.SourceSignature = "Blueprint:" + std::string(Entry.bHook ? "float(float)" : "void(float)");
			if (!IsGuid(Guid) || !Guids.insert(Guid).second || !Validator::IsVersion(Entry.Version))
			{
				throw std::runtime_error(Entry.Location + ": invalid or duplicate extension identity.");
			}
			if (Entry.bHook)
			{
				Entry.Input = GenerationFields::Required(Item, "input");
				Entry.Output = GenerationFields::Required(Item, "output");
				Entry.OutputRole = GenerationFields::Required(Item, "outputRole");
				Entry.bPre = Item.at("pre").get<bool>();
				Entry.bPost = Item.at("post").get<bool>();
				const auto Operations = Item.at("operations").get<int>();
				if (Operations < 0 || Operations > 7 || (!Entry.bPre && !Entry.bPost) ||
				    (!Entry.bPre && (Operations & 3)) || (!Entry.bPost && (Operations & 4)) ||
				    (Entry.OutputRole != "Return" && Entry.OutputRole != "Output"))
				{
					throw std::runtime_error(Entry.Location + ": invalid Hook policy or signature.");
				}
				Entry.Operations = static_cast<std::uint8_t>(Operations);
			}
			AddExtension(Model, std::move(Entry));
		}
		for (const auto& Entry : Record.at("apis"))
		{
			auto Api = ReadAPI(Entry, Type, Source, Asset, Type.APIs.size(), Guids);
			if (!Ids.insert(Api.Id).second || !Type.MethodNames.insert(ExportNameRules::Fold(Api.ExportName)).second)
			{
				throw std::runtime_error(Api.NameLocation + ": duplicate Blueprint API identity or public name.");
			}
			Type.APIs.push_back(Model.APIs.size());
			Model.APIs.push_back(std::move(Api));
		}
		if (Type.APIs.empty())
		{
			continue;
		}
		const auto Key = Type.Identity.CppName;
		if (!Model.Types.emplace(Key, std::move(Type)).second)
		{
			throw std::runtime_error(Source + ": generated Blueprint type collides with a C++ type.");
		}
	}
}
} // namespace Mod::HeaderTool::BlueprintExports
