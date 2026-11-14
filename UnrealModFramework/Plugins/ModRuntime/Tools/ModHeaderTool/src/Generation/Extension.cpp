// Copyright (c) 2026 Nelaric Contributors
#include "Extension.h"

#include "ModHeaderTool/src/Generation/GenerationModel.h"
#include "ModHeaderTool/src/Hash/Hash.h"

#include <cctype>
#include <stdexcept>

namespace Mod::HeaderTool
{
nlohmann::json Extension::Describe() const
{
	return {{"id", Id},
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
	        {"inputType", "float"},
	        {"resultType", bHook ? "float" : "void"}};
}

std::string Extension::Signature() const
{
	return Hash::HashBytes(Describe().dump());
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
