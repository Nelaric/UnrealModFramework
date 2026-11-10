// Copyright (c) 2026 Nelaric Contributors
#include "MetadataSnapshot.h"

#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Files/TextFile.h"
#include "ModHeaderTool/src/Metadata/MetadataOptionsRefreshRequired.h"

#include "ModExportMetadataSchema.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <map>
#include <nlohmann/json.hpp>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
namespace Schema = Mod::ExportMetadata::Schema;

std::string MetadataSnapshot::InputPath(const fs::path& Path)
{
	if (!Path.is_absolute())
	{
		throw std::runtime_error("Metadata option input paths must be absolute.");
	}
	return Path::PathToUtf8(fs::weakly_canonical(Path));
}

std::string MetadataSnapshot::FileFingerprint(const fs::path& Path)
{
	if (!fs::exists(Path))
	{
		return {};
	}
	std::ostringstream Result;
	Result << std::hex << std::setfill('0') << std::setw(16) << Schema::Fingerprint(TextFile::ReadTextFile(Path));
	return Result.str();
}

std::vector<fs::path> MetadataSnapshot::PathArray(const Json& Document, const char* Field)
{
	if (!Document.contains(Field) || !Document[Field].is_array())
	{
		throw std::runtime_error(std::string("Metadata options require a '") + Field + "' path array.");
	}
	std::vector<fs::path> Result;
	for (const Json& Value : Document[Field])
	{
		if (!Value.is_string() || Value.get_ref<const std::string&>().empty())
		{
			throw std::runtime_error(std::string("Invalid path in metadata '") + Field + "'.");
		}
		const fs::path Path = Path::PathFromUtf8(Value.get_ref<const std::string&>());
		(void)InputPath(Path);
		Result.push_back(Path);
	}
	return Result;
}

void MetadataSnapshot::CheckInputs(const Json& Document)
{
	if (!Document.contains("Inputs") || !Document["Inputs"].is_object())
	{
		throw std::runtime_error("Metadata options require configuration input fingerprints.");
	}
	std::map<std::string, std::string> Expected;
	for (const auto& Input : Document["Inputs"].items())
	{
		if (!Input.value().is_string())
		{
			throw std::runtime_error("Invalid metadata input fingerprint.");
		}
		Expected.emplace(InputPath(Path::PathFromUtf8(Input.key())), Input.value().get<std::string>());
	}
	std::map<std::string, std::string> Current;
	for (const fs::path& Root : PathArray(Document, "ConfigRoots"))
	{
		if (!fs::exists(Root))
		{
			continue;
		}
		for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(Root))
		{
			std::string Extension = Path::PathToUtf8(Entry.path().extension());
			std::transform(Extension.begin(), Extension.end(), Extension.begin(),
			               [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
			if (Entry.is_regular_file() && Extension == ".ini")
			{
				Current.emplace(InputPath(Entry.path()), FileFingerprint(Entry.path()));
			}
		}
	}
	for (const fs::path& Input : PathArray(Document, "InputFiles"))
	{
		Current.emplace(InputPath(Input), FileFingerprint(Input));
	}
	if (Current != Expected)
	{
		std::string Changed;
		for (const auto& [Path, Hash] : Current)
		{
			const auto Existing = Expected.find(Path);
			if (Existing == Expected.end() || Existing->second != Hash)
			{
				Changed = Path;
				break;
			}
		}
		if (Changed.empty())
		{
			for (const auto& [Path, Hash] : Expected)
			{
				if (!Current.contains(Path))
				{
					Changed = Path;
					break;
				}
			}
		}
		throw MetadataOptionsRefreshRequired("Metadata options are stale because an input changed: " +
		                                         DiagnosticLocation::QuoteDiagnosticPath(Path::PathFromUtf8(Changed)),
		                                     RefreshHint);
	}
}
} // namespace Mod::HeaderTool
