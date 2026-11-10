// Copyright (c) 2026 Nelaric Contributors
#include "RedirectReader.h"

#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Files/Path.h"

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace Mod::HeaderTool
{
using Json = nlohmann::json;

RedirectFunctionName RedirectReader::RedirectName(const Json& Name)
{
	return {Name.at("Package").get<std::string>(), Name.at("Class").get<std::string>(),
	        Name.at("Function").get<std::string>()};
}

std::string RedirectReader::RedirectLocation(std::string_view Location)
{
	const auto Start = Location.rfind('(');
	if (Start != Location.npos && Location.ends_with(')'))
	{
		const auto Coordinates = Location.substr(Start + 1, Location.size() - Start - 2);
		if (Coordinates.find(',') != Coordinates.npos &&
		    Coordinates.find_first_not_of("0123456789,") == Coordinates.npos)
		{
			return DiagnosticLocation::QuoteDiagnosticPath(Path::PathFromUtf8(Location.substr(0, Start))) +
			       std::string(Location.substr(Start));
		}
	}
	return DiagnosticLocation::QuoteDiagnosticPath(Path::PathFromUtf8(Location));
}
} // namespace Mod::HeaderTool
