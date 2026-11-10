// Copyright (c) 2026 Nelaric Contributors
#include "ArchiveWitness.h"

#include "Common/Hash/Hash.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::BuildTool
{
std::vector<std::string> ArchiveWitness::ArchiveSymbols(const Request& Request, const std::string& Witness)
{
	auto Symbols = Request.RequiredSymbols;
	if (Request.bPeLinkWitness)
	{
		if (!Witness.starts_with(LinkWitnessPrefix))
		{
			throw std::runtime_error("Host archive is missing its PE link witness.");
		}
		Hash::RequireDigest(std::string_view(Witness).substr(LinkWitnessPrefix.size()), "linkWitness");
		Symbols.push_back(Witness);
	}
	return Symbols;
}
} // namespace Mod::BuildTool
