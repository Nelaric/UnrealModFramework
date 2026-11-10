// Copyright (c) 2026 Nelaric Contributors
#include "GenerationOutput.h"

#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Files/TextFile.h"
#include "ModHeaderTool/src/Generation/Artifacts/OutputFiles.h"
#include "ModHeaderTool/src/Generation/Artifacts/OutputLock.h"
#include "ModHeaderTool/src/Generation/Artifacts/OutputPaths.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

void GenerationOutput::PublishGeneratedFiles(const fs::path& Directory, const std::map<std::string, std::string>& Files,
                                             const std::string& ManifestName)
{
	const fs::path Root = fs::weakly_canonical(fs::absolute(Directory));
	fs::create_directories(Root);
	OutputLock Lock(Root);
	std::vector<fs::path> Stale, Directories;
	for (const char* Name : {"SDK", "Host", "Client", "Contract", "Diagnostics"})
	{
		const fs::path Managed = OutputPaths::CheckedPath(Root, Name);
		if (!fs::exists(Managed))
		{
			continue;
		}
		Directories.push_back(Managed);
		for (const auto& Entry : fs::recursive_directory_iterator(Managed))
		{
			const fs::path Relative = Entry.path().lexically_relative(Root);
			OutputPaths::CheckedPath(Root, Relative);
			if (Entry.is_symlink())
			{
				throw std::runtime_error("Generated output directories cannot contain symbolic links.");
			}
			if (Entry.is_directory())
			{
				Directories.push_back(Entry.path());
			}
			else if (Entry.is_regular_file() &&
			         std::none_of(Files.begin(), Files.end(), [&](const auto& File)
			                      { return OutputPaths::SamePart(Relative, Path::PathFromUtf8(File.first)); }))
			{
				Stale.push_back(Entry.path());
			}
		}
	}
	for (const auto& Entry : fs::directory_iterator(Root))
	{
		if (Entry.is_regular_file() && Entry.path().filename() != Path::PathFromUtf8(ManifestName) &&
		    Path::PathToUtf8(Entry.path().filename()).ends_with(".generated-artifacts.json"))
		{
			const auto Previous = nlohmann::json::parse(TextFile::ReadTextFile(Entry.path()), nullptr, false);
			if (Previous.is_object() && Previous.value("format", "") == "UMF.GeneratedArtifacts" &&
			    Previous.value("outputLayout", "") == "Flat")
			{
				Stale.push_back(OutputPaths::CheckedPath(Root, Entry.path().filename()));
			}
		}
	}
	bool bChanged = !Stale.empty();
	for (const auto& [Name, Bytes] : Files)
	{
		const fs::path Destination = OutputPaths::CheckedPath(Root, Path::PathFromUtf8(Name));
		bChanged |= !fs::is_regular_file(Destination) || TextFile::ReadTextFile(Destination) != Bytes;
	}
	if (!bChanged)
	{
		return;
	}
	const auto Unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	const fs::path Staging = OutputPaths::CheckedPath(Root, ".mht-tmp-" + Unique);
	if (!fs::create_directory(Staging))
	{
		throw std::runtime_error("Cannot create MHT temporary output directory.");
	}
	try
	{
		std::map<std::string, fs::path> Staged;
		for (const auto& [Name, Bytes] : Files)
		{
			const fs::path File = Staging / std::to_string(Staged.size());
			OutputFiles::Write(File, Bytes);
			Staged.emplace(Name, File);
		}
		auto Pending = nlohmann::json::parse(Files.at(ManifestName));
		Pending["status"] = "Generating";
		OutputFiles::Write(Staging / "Pending.json", Pending.dump(2) + '\n');
		OutputFiles::Replace(Staging / "Pending.json",
		                     OutputPaths::CheckedPath(Root, Path::PathFromUtf8(ManifestName)));
		for (const auto& [Name, Bytes] : Files)
		{
			if (Name == ManifestName)
			{
				continue;
			}
			const fs::path Destination = OutputPaths::CheckedPath(Root, Path::PathFromUtf8(Name));
			if (!fs::is_regular_file(Destination) || TextFile::ReadTextFile(Destination) != Bytes)
			{
				fs::create_directories(Destination.parent_path());
				OutputFiles::Replace(Staged.at(Name), Destination);
			}
		}
		for (const fs::path& File : Stale)
		{
			fs::remove(OutputPaths::CheckedPath(Root, File.lexically_relative(Root)));
		}
		std::sort(Directories.begin(), Directories.end(), [](const fs::path& Left, const fs::path& Right)
		          { return std::distance(Left.begin(), Left.end()) > std::distance(Right.begin(), Right.end()); });
		for (const fs::path& StaleDirectory : Directories)
		{
			const fs::path Path = OutputPaths::CheckedPath(Root, StaleDirectory.lexically_relative(Root));
			if (fs::is_empty(Path))
			{
				fs::remove(Path);
			}
		}
		OutputFiles::Replace(Staged.at(ManifestName), OutputPaths::CheckedPath(Root, Path::PathFromUtf8(ManifestName)));
	}
	catch (...)
	{
		std::error_code Ignored;
		fs::remove_all(OutputPaths::CheckedPath(Root, Staging.filename()), Ignored);
		throw;
	}
	fs::remove_all(OutputPaths::CheckedPath(Root, Staging.filename()));
}
} // namespace Mod::HeaderTool
