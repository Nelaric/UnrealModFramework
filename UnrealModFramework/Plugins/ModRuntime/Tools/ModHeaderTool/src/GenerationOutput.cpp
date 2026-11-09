// Copyright (c) 2026 Nelaric Contributors
#include "GenerationOutput.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace Mod::HeaderTool
{
namespace
{
bool SamePart(const fs::path& Left, const fs::path& Right)
{
#ifdef _WIN32
	const fs::path NormalLeft = Left.lexically_normal().make_preferred();
	const fs::path NormalRight = Right.lexically_normal().make_preferred();
	return CompareStringOrdinal(NormalLeft.c_str(), -1, NormalRight.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
	return Left == Right;
#endif
}

fs::path CheckedPath(const fs::path& Root, const fs::path& Relative)
{
	if (Relative.empty() || Relative.is_absolute() || Relative.has_root_name())
	{
		throw std::runtime_error("Generated output paths must be relative.");
	}
	for (const fs::path& Part : Relative)
	{
		if (Part == "..")
		{
			throw std::runtime_error("Generated output paths cannot contain '..'.");
		}
	}
	const fs::path Result = fs::weakly_canonical(Root / Relative);
	auto Part = Result.begin();
	for (const fs::path& RootPart : Root)
	{
		if (RootPart.empty())
		{
			continue;
		}
		if (Part == Result.end() || !SamePart(*Part++, RootPart))
		{
			throw std::runtime_error("Generated output escapes its directory: " + PathToUtf8(Relative));
		}
	}
	if (Part == Result.end())
	{
		throw std::runtime_error("A generated output cannot replace its root.");
	}
	return Result;
}

class FOutputLock
{
public:
	explicit FOutputLock(const fs::path& Root)
	{
		const fs::path File = CheckedPath(Root, ".mht.lock");
#ifdef _WIN32
		Handle = CreateFileW(File.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
		                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
		if (Handle == INVALID_HANDLE_VALUE)
#else
		Handle = open(File.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
		if (Handle < 0 || flock(Handle, LOCK_EX | LOCK_NB) != 0)
#endif
		{
#ifndef _WIN32
			if (Handle >= 0)
			{
				close(Handle);
			}
#endif
			throw std::runtime_error("Cannot lock MHT output directory: " + PathToUtf8(Root));
		}
	}
	~FOutputLock()
	{
#ifdef _WIN32
		CloseHandle(Handle);
#else
		close(Handle);
#endif
	}
	FOutputLock(const FOutputLock&) = delete;
	FOutputLock& operator=(const FOutputLock&) = delete;

private:
#ifdef _WIN32
	HANDLE Handle = INVALID_HANDLE_VALUE;
#else
	int Handle = -1;
#endif
};

void Write(const fs::path& File, const std::string& Bytes)
{
	fs::create_directories(File.parent_path());
	std::ofstream Stream(File, std::ios::binary | std::ios::trunc);
	Stream.write(Bytes.data(), static_cast<std::streamsize>(Bytes.size()));
	Stream.close();
	if (!Stream)
	{
		throw std::runtime_error("Cannot write generated output: " + PathToUtf8(File));
	}
}

void Replace(const fs::path& Source, const fs::path& Destination)
{
#ifdef _WIN32
	if (!MoveFileExW(Source.c_str(), Destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		throw std::runtime_error("Cannot replace generated output: " + PathToUtf8(Destination));
	}
#else
	fs::rename(Source, Destination);
#endif
}
} // namespace

void PublishGeneratedFiles(const fs::path& Directory, const std::map<std::string, std::string>& Files,
                           const std::string& ManifestName)
{
	const fs::path Root = fs::weakly_canonical(fs::absolute(Directory));
	fs::create_directories(Root);
	FOutputLock Lock(Root);
	std::vector<fs::path> Stale, Directories;
	for (const char* Name : {"SDK", "Host", "Client", "Contract", "Diagnostics"})
	{
		const fs::path Managed = CheckedPath(Root, Name);
		if (!fs::exists(Managed))
		{
			continue;
		}
		Directories.push_back(Managed);
		for (const auto& Entry : fs::recursive_directory_iterator(Managed))
		{
			const fs::path Relative = Entry.path().lexically_relative(Root);
			CheckedPath(Root, Relative);
			if (Entry.is_symlink())
			{
				throw std::runtime_error("Generated output directories cannot contain symbolic links.");
			}
			if (Entry.is_directory())
			{
				Directories.push_back(Entry.path());
			}
			else if (Entry.is_regular_file() && std::none_of(Files.begin(), Files.end(), [&](const auto& File)
			                                                 { return SamePart(Relative, PathFromUtf8(File.first)); }))
			{
				Stale.push_back(Entry.path());
			}
		}
	}
	for (const auto& Entry : fs::directory_iterator(Root))
	{
		if (Entry.is_regular_file() && Entry.path().filename() != PathFromUtf8(ManifestName) &&
		    PathToUtf8(Entry.path().filename()).ends_with(".generated-artifacts.json"))
		{
			const auto Previous = nlohmann::json::parse(ReadTextFile(Entry.path()), nullptr, false);
			if (Previous.is_object() && Previous.value("format", "") == "UMF.GeneratedArtifacts" &&
			    Previous.value("outputLayout", "") == "Flat")
			{
				Stale.push_back(CheckedPath(Root, Entry.path().filename()));
			}
		}
	}
	bool bChanged = !Stale.empty();
	for (const auto& [Name, Bytes] : Files)
	{
		const fs::path Destination = CheckedPath(Root, PathFromUtf8(Name));
		bChanged |= !fs::is_regular_file(Destination) || ReadTextFile(Destination) != Bytes;
	}
	if (!bChanged)
	{
		return;
	}
	const auto Unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	const fs::path Staging = CheckedPath(Root, ".mht-tmp-" + Unique);
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
			Write(File, Bytes);
			Staged.emplace(Name, File);
		}
		auto Pending = nlohmann::json::parse(Files.at(ManifestName));
		Pending["status"] = "Generating";
		Write(Staging / "Pending.json", Pending.dump(2) + '\n');
		Replace(Staging / "Pending.json", CheckedPath(Root, PathFromUtf8(ManifestName)));
		for (const auto& [Name, Bytes] : Files)
		{
			if (Name == ManifestName)
			{
				continue;
			}
			const fs::path Destination = CheckedPath(Root, PathFromUtf8(Name));
			if (!fs::is_regular_file(Destination) || ReadTextFile(Destination) != Bytes)
			{
				fs::create_directories(Destination.parent_path());
				Replace(Staged.at(Name), Destination);
			}
		}
		for (const fs::path& File : Stale)
		{
			fs::remove(CheckedPath(Root, File.lexically_relative(Root)));
		}
		std::sort(Directories.begin(), Directories.end(), [](const fs::path& Left, const fs::path& Right)
		          { return std::distance(Left.begin(), Left.end()) > std::distance(Right.begin(), Right.end()); });
		for (const fs::path& StaleDirectory : Directories)
		{
			const fs::path Path = CheckedPath(Root, StaleDirectory.lexically_relative(Root));
			if (fs::is_empty(Path))
			{
				fs::remove(Path);
			}
		}
		Replace(Staged.at(ManifestName), CheckedPath(Root, PathFromUtf8(ManifestName)));
	}
	catch (...)
	{
		std::error_code Ignored;
		fs::remove_all(CheckedPath(Root, Staging.filename()), Ignored);
		throw;
	}
	fs::remove_all(CheckedPath(Root, Staging.filename()));
}
} // namespace Mod::HeaderTool
