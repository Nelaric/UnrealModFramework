// Copyright (c) 2026 Nelaric Contributors
#include "OutputPaths.h"

#include "ModHeaderTool/src/Files/DiagnosticLocation.h"

#include <filesystem>
#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

bool OutputPaths::SamePart(const fs::path& Left, const fs::path& Right)
{
#ifdef _WIN32
	const fs::path NormalLeft = Left.lexically_normal().make_preferred();
	const fs::path NormalRight = Right.lexically_normal().make_preferred();
	return CompareStringOrdinal(NormalLeft.c_str(), -1, NormalRight.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
	return Left == Right;
#endif
}

fs::path OutputPaths::CheckedPath(const fs::path& Root, const fs::path& Relative)
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
			throw std::runtime_error("Generated output escapes its directory: " +
			                         DiagnosticLocation::QuoteDiagnosticPath(Relative));
		}
	}
	if (Part == Result.end())
	{
		throw std::runtime_error("A generated output cannot replace its root.");
	}
	return Result;
}
} // namespace Mod::HeaderTool
