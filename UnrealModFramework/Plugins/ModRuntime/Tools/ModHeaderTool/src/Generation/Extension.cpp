// Copyright (c) 2026 Nelaric Contributors
#include "Extension.h"

#include "ModHeaderTool/src/Generation/GenerationModel.h"
#include "ModHeaderTool/src/Hash/Hash.h"

#include <cctype>
#include <stdexcept>

namespace Mod::HeaderTool
{
std::string Extension::BlueprintPayloadType() const
{
	return "ModExtensionPayload_" + Hash::HashBytes(Id).substr(0, 16);
}

nlohmann::json Extension::Describe() const
{
	nlohmann::json Record{{"id", Id},
	                      {"kind", bHook ? "Hook" : "Event"},
	                      {"version", Version},
	                      {"scope", "World"},
	                      {"thread", "GameThread"},
	                      {"authorityOnly", bAuthorityOnly},
	                      {"permission", Permission},
	                      {"publishPermission", PublishPermission},
	                      {"ordered", true},
	                      {"pre", bPre},
	                      {"post", bPost},
	                      {"operations", Operations},
	                      {"inputType", Scalar},
	                      {"resultType", bHook ? Scalar : "void"}};
	if (!PayloadKind.empty())
	{
		Record["payload"] = {{"kind", PayloadKind},
		                     {"type", PayloadPath.empty() ? PayloadCpp : PayloadPath},
		                     {"layout", PayloadLayout},
		                     {"viewId", PayloadTypeId}};
		if (PayloadKind == "Struct")
		{
			Record["payload"]["blueprintType"] = BlueprintPayloadType();
		}
		if (!PayloadViewSignature.empty())
		{
			Record["payload"]["viewSignature"] = PayloadViewSignature;
		}
		Record["inputType"] = PayloadKind;
		Record["resultType"] = bHook ? PayloadKind : "void";
	}
	if (bOn)
	{
		Record["on"] = true;
	}
	if (bQueued)
	{
		Record["delivery"] = "NextTick";
	}
	if (bImmediateUnsubscribe)
	{
		Record["immediateUnsubscribe"] = true;
	}
	return Record;
}

std::string Extension::Signature() const
{
	return Hash::HashBytes(Describe().dump());
}

bool IsExtensionScalar(std::string_view Type)
{
	return Type == "float" || Type == "double" || Type == "int32" || Type == "int64" || Type == "bool" ||
	       Type == "uint8";
}

void AddExtension(GenerationModel& Model, Extension Entry)
{
	const auto Prefix = Entry.bHook ? "hook/" : "event/";
	if (!Entry.Id.starts_with(Prefix) || Entry.Id.size() <= std::string_view(Prefix).size() || Entry.Id.size() > 128 ||
	    Entry.Id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-._/") != std::string::npos)
	{
		throw std::runtime_error(Entry.Location + ": invalid stable extension ID: " + Entry.Id);
	}
	const auto Found = Model.Extensions.find(Entry.Id);
	if (Found != Model.Extensions.end())
	{
		const auto& Previous = Found->second;
		if (Previous.Location != Entry.Location || Previous.SourceSignature != Entry.SourceSignature ||
		    Previous.Signature() != Entry.Signature())
		{
			throw std::runtime_error(Entry.Location + ": duplicate or inconsistent extension ID " + Entry.Id +
			                         " (also at " + Previous.Location + ").");
		}
		return;
	}
	const auto Id = Entry.Id;
	Model.Extensions.emplace(Id, std::move(Entry));
}
} // namespace Mod::HeaderTool
