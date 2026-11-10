// Copyright (c) 2026 Nelaric Contributors
#include "ScanDocument.h"

#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Project/Project.h"

#include "ModExportMetadataSchema.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

std::string ScanDocument::DisplaySourcePath(const fs::path& Path, const fs::path& Root)
{
	const fs::path Relative = Path.lexically_relative(Root);
	if (!Relative.empty() && *Relative.begin() != "..")
	{
		return Path::PathToUtf8(Relative);
	}
	return Path::PathToUtf8(Path);
}

void ScanDocument::WriteDocument(const std::string& Document, const fs::path& Output, const ScanResult& Result)
{
	if (Output.empty())
	{
		std::cout << Document << '\n';
		if (!std::cout)
		{
			throw std::runtime_error("Cannot write scan result to stdout.");
		}
		return;
	}
	const fs::path Destination = fs::absolute(Output).lexically_normal();
	if (fs::exists(Destination))
	{
		bool bIsInput = fs::equivalent(Destination, Result.Project.File) ||
		                (Result.Project.bHasConfig && fs::equivalent(Destination, Result.Project.ConfigFile));
		const fs::path Catalog = Result.MetadataOptions.empty()
		                             ? Result.Project.Root / Mod::ExportMetadata::Schema::SnapshotPath
		                             : Result.MetadataOptions;
		bIsInput = bIsInput || (fs::exists(Catalog) && fs::equivalent(Destination, Catalog));
		for (const fs::path& SourceFile : Result.SourceFiles)
		{
			bIsInput = bIsInput || fs::equivalent(Destination, SourceFile);
		}
		if (bIsInput)
		{
			throw std::runtime_error("Output would overwrite a scan input: " +
			                         DiagnosticLocation::QuoteDiagnosticPath(Destination));
		}
	}
	fs::create_directories(Destination.parent_path());
	std::ofstream File(Destination, std::ios::binary | std::ios::trunc);
	File << Document << '\n';
	File.close();
	if (!File)
	{
		throw std::runtime_error("Cannot write scan result: " + DiagnosticLocation::QuoteDiagnosticPath(Destination));
	}
}
} // namespace Mod::HeaderTool
