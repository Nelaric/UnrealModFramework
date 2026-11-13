// Copyright (c) 2026 Nelaric Contributors
#include "ProjectionWriter.h"

#include "Common/Errors/ErrorCodes.h"
#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Generation/APIParameter.h"
#include "ModHeaderTool/src/Generation/Artifacts/CppSignature.h"
#include "ModHeaderTool/src/Generation/Artifacts/GenerationPaths.h"
#include "ModHeaderTool/src/Generation/Artifacts/LifecycleExport.h"
#include "ModHeaderTool/src/Generation/GenerationInput.h"
#include "ModHeaderTool/src/Generation/ResolvedType.h"
#include "ModHeaderTool/src/Generation/TypeIdentity.h"
#include "ModHeaderTool/src/Hash/Hash.h"
#include "ModHeaderTool/src/Metadata/MetadataField.h"
#include "ModHeaderTool/src/Metadata/MetadataValue.h"
#include "ModHeaderTool/src/Scan/ExportMacro.h"

#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace Mod::HeaderTool
{
using Json = nlohmann::json;

Json ProjectionWriter::TypeJson(const GenerationModel& Model, const APIType& Type)
{
	if (Type.Kind == APIType::EKind::Object)
	{
		return {{"kind", "ObjectView"},
		        {"typeId", Model.Types.at(Type.Object).Identity.Id},
		        {"readOnly", Type.bConst},
		        {"ownership", "Weak"},
		        {"null", "ErrorIfNull"}};
	}
	if (Type.Kind == APIType::EKind::ObjectInput)
	{
		return {{"kind", "ObjectInput"}, {"cppType", Type.Public}, {"ownership", "Borrowed"}};
	}
	return {{"kind", Type.Kind == APIType::EKind::Void ? "Void" : "Scalar"}, {"cppType", Type.Public}};
}

Json ProjectionWriter::APIJson(const GenerationModel& Model, const API& API)
{
	Json Metadata = Json::object();
	for (const auto& Field : API.Macro.Fields)
	{
		if (Field.Value.Kind == MetadataValue::EKind::List)
		{
			Json Values = Json::array();
			for (const auto& Item : Field.Value.Items)
			{
				Values.push_back(Item.Text);
			}
			Metadata[Field.Name] = Values;
		}
		else
		{
			Metadata[Field.Name] = Field.Value.Text;
		}
	}
	Json Params = Json::array();
	for (const auto& Param : API.Parameters)
	{
		Params.push_back(TypeJson(Model, Param.Type));
	}
	const Json Signature{{"name", API.ExportName},
	                     {"const", API.bConst},
	                     {"parameters", Params},
	                     {"returns", TypeJson(Model, API.Return)},
	                     {"failureRule", "ReturnError"}};
	Json Policy{{"scope", API.Scope},
	            {"thread", "GameThread"},
	            {"permissions", API.Permissions},
	            {"failureRule", "ReturnError"}};
	if (API.bFullViewMember)
	{
		Policy["fullViewMember"] = true;
	}
	return {{"kind", "API"},
	        {"metadata", Metadata},
	        {"exportId", API.Id},
	        {"exportVersion", API.Version},
	        {"ownerTypeId", Model.Types.at(API.Owner).Identity.Id},
	        {"signature", Signature},
	        {"policy", Policy},
	        {"sourceSignatureHash", Hash::HashBytes(API.SourceSignature)},
	        {"signatureHash", Hash::HashBytes(Signature.dump())},
	        {"projectionHash", Hash::HashBytes(Signature.dump() + ":UObjectAPIProjection1")},
	        {"policyHash", Hash::HashBytes(Policy.dump())}};
}

Json ProjectionWriter::DescribeTypes(GenerationModel& Model)
{
	Json Types = Json::array();
	for (auto& [Name, Type] : Model.Types)
	{
		Json Members = Json::array();
		std::set<std::string> Shapes;
		for (auto Index : Type.APIs)
		{
			const auto& API = Model.APIs[Index];
			std::string Shape =
			    API.ExportName + "(" + CppSignature::Parameters(API) + ")" + (API.bConst ? " const" : "");
			if (!Shapes.insert(Shape).second)
			{
				throw std::runtime_error("Projected overload collision: " + Name + "::" + Shape);
			}
			auto Member = APIJson(Model, API);
			Member.erase("metadata");
			Members.push_back(std::move(Member));
		}
		Json TypeContract{
		    {"typeId", Type.Identity.Id}, {"scope", Type.Identity.Scope}, {"apis", Members}, {"projectionVersion", 1}};
		if (Type.bFullView)
		{
			TypeContract["view"] = "Full";
		}
		if (Type.bAllowWorldless)
		{
			TypeContract["allowWorldless"] = true;
		}
		Type.Signature = Hash::HashBytes(TypeContract.dump());
		TypeContract.erase("apis");
		TypeContract.erase("projectionVersion");
		TypeContract["cppName"] = Name;
		TypeContract["callingTableHash"] = Type.Signature;
		Types.push_back(std::move(TypeContract));
	}
	return Types;
}

Projection ProjectionWriter::BuildProjection(GenerationModel& Model)
{
	const auto Types = DescribeTypes(Model);
	Projection Projection;
	Json Exports = Json::array();
	for (const auto& API : Model.APIs)
	{
		auto Export = APIJson(Model, API);
		if (API.SourceErrorCodes)
		{
			Export["sourceErrorCodes"] = *API.SourceErrorCodes;
		}
		Exports.push_back(std::move(Export));
		Json Source{{"exportId", API.Id},
		            {"module", API.Module},
		            {"sourceSignature", API.SourceSignature},
		            {"file", GenerationPaths::NormalizeInput(Model.Input, Path::PathToUtf8(API.Macro.File))}};
		if (API.Blueprint)
		{
			Source["class"] = Model.Types.at(API.Owner).BlueprintClass;
			Source["function"] = API.Name;
		}
		else
		{
			Source["line"] = API.Macro.Line;
			Source["column"] = API.Macro.Column;
		}
		Projection.Sources.push_back(std::move(Source));
	}
	Projection.Contract = Json{{"schemaVersion", 1},
	                           {"projectionVersion", 1},
	                           {"gameId", Model.Input.GameId},
	                           {"objectReturnPolicy", "ErrorIfNull"},
	                           {"types", Types},
	                           {"exports", Exports}};
	if (Model.ErrorCodeEncodingVersion == Tools::ErrorCodes::StructuredErrorCodeVersion)
	{
		Projection.ErrorHeader = "SDK/Public/" + Model.Input.GameId + ".moderrors.h";
		Projection.Contract["errorCodeEncodingVersion"] = Model.ErrorCodeEncodingVersion;
		Projection.Contract["errorHeader"] = Projection.ErrorHeader;
	}
	if (!Model.LifecycleSource.empty())
	{
		Projection.LifecycleExport = LifecycleExport::ExportLifecycleDeclarations(Model.LifecycleSource);
		Projection.Lifecycle = {{"schemaVersion", 1},
		                        {"module", "ModLoader"},
		                        {"resource", LifecycleExport::LifecyclePath},
		                        {"resourceHash", Hash::HashBytes(Projection.LifecycleExport)}};
		Projection.Contract["modLifecycle"] = Projection.Lifecycle;
	}
	Json Compatibility = Projection.Contract;
	for (auto& API : Compatibility["exports"])
	{
		API.erase("metadata");
	}
	Projection.ContractHash = Hash::HashBytes(Compatibility.dump());
	Projection.ProjectionHash = Hash::HashBytes(Types.dump() + "UObjectAPIProjection1:ContextHandlesV1" +
	                                            (Projection.Lifecycle.is_null() ? "" : Projection.Lifecycle.dump()));
	return Projection;
}
} // namespace Mod::HeaderTool
