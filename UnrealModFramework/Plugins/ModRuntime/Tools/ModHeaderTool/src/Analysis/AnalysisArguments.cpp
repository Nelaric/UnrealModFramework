// Copyright (c) 2026 Nelaric Contributors
#include "AnalysisArguments.h"

#include "ModHeaderTool/src/Analysis/CursorSource.h"
#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Files/TextFile.h"
#include "ModHeaderTool/src/Generation/Artifacts/GenerationPaths.h"
#include "ModHeaderTool/src/Generation/GenerationInput.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

std::vector<std::string> AnalysisArguments::SplitArguments(std::string_view Text)
{
	std::vector<std::string> Result;
	std::string Current;
	char Quote = 0;
	bool bToken = false;
	for (std::size_t I = 0; I < Text.size(); ++I)
	{
		const char C = Text[I];
		if (C == '\\' && I + 1 < Text.size() && Text[I + 1] == '"')
		{
			Current.push_back('"');
			++I;
			bToken = true;
		}
		else if (C == '"' || C == '\'')
		{
			if (Quote == C)
			{
				Quote = 0;
			}
			else if (!Quote)
			{
				Quote = C;
			}
			else
			{
				Current.push_back(C);
			}
			bToken = true;
		}
		else if (!Quote && std::isspace(static_cast<unsigned char>(C)))
		{
			if (bToken)
			{
				Result.push_back(std::move(Current));
				Current.clear();
				bToken = false;
			}
		}
		else
		{
			Current.push_back(C);
			bToken = true;
		}
	}
	if (Quote)
	{
		throw std::runtime_error("Unclosed quote in compiler arguments.");
	}
	if (bToken)
	{
		Result.push_back(std::move(Current));
	}
	return Result;
}

void AnalysisArguments::ExpandResponses(const std::vector<std::string>& Arguments, const fs::path& Directory,
                                        std::vector<std::string>& Expanded, std::set<fs::path>& Stack,
                                        GenerationModel& Model)
{
	for (const auto& Argument : Arguments)
	{
		if (!Argument.starts_with('@'))
		{
			Expanded.push_back(Argument);
			continue;
		}
		const fs::path Response = fs::canonical(Directory / Path::PathFromUtf8(Argument.substr(1)));
		if (!Stack.insert(Response).second)
		{
			throw std::runtime_error("Recursive compiler response file.");
		}
		Model.Dependencies.insert(Response);
		ExpandResponses(SplitArguments(TextFile::ReadTextFile(Response)), Directory, Expanded, Stack, Model);
		Stack.erase(Response);
	}
}

std::vector<std::string> AnalysisArguments::AnalysisCommand(const std::vector<std::string>& Command,
                                                            const fs::path& Directory, const fs::path& Unit,
                                                            GenerationModel& Model)
{
	std::vector<std::string> Expanded;
	std::set<fs::path> Stack;
	ExpandResponses(Command, Directory, Expanded, Stack, Model);
	if (Expanded.empty())
	{
		throw std::runtime_error("Empty compilation command.");
	}
	for (const auto& Argument : Expanded)
	{
		if (Argument.starts_with("/Yu") || Argument == "-include-pch")
		{
			throw std::runtime_error(
			    "Analysis requires fresh NoPCH compiler inputs. Run UBT -Mode=GenerateClangDatabase -NoPCH immediately "
			    "before MHT; a normal build may replace its response files.");
		}
	}
	std::string Driver = Path::PathToUtf8(Path::PathFromUtf8(Expanded.front()).filename());
	std::transform(Driver.begin(), Driver.end(), Driver.begin(), [](unsigned char C) { return std::tolower(C); });
	const bool bMsvc = Driver == "cl.exe" || Driver == "clang-cl.exe";
	std::vector<std::string> Result{"clang", "-x", "c++", "-working-directory", Path::PathToUtf8(Directory)};
	if (bMsvc)
	{
		Result.insert(Result.end(), {"-fms-extensions", "-fms-compatibility", "-fdelayed-template-parsing"});
		for (std::size_t I = 1; I < Expanded.size(); ++I)
		{
			const std::string& Arg = Expanded[I];
			bool bConsumed = false;
			for (const auto& [Prefix, Replacement] : std::vector<std::pair<std::string, std::string>>{
			         {"/external:I", "-isystem"}, {"/FI", "-include"}, {"/I", "-I"}, {"/D", "-D"}, {"/U", "-U"}})
			{
				if (!Arg.starts_with(Prefix))
				{
					continue;
				}
				const std::string Value = Arg.size() == Prefix.size() ? Expanded.at(++I) : Arg.substr(Prefix.size());
				Result.push_back(Replacement);
				Result.push_back(Value);
				bConsumed = true;
				break;
			}
			if (bConsumed)
			{
				continue;
			}
			if (Arg.starts_with("/std:"))
			{
				Result.push_back("-std=" + Arg.substr(5));
			}
			else if (Arg == "/J")
			{
				Result.push_back("-funsigned-char");
			}
			else if (Arg == "/GR-")
			{
				Result.push_back("-fno-rtti");
			}
			else if (Arg == "/GR")
			{
				Result.push_back("-frtti");
			}
			else if (Arg.starts_with("/EH"))
			{
				Result.push_back("-fexceptions");
			}
			else if (Arg.starts_with("/Zp"))
			{
				Result.push_back("-fpack-struct=" + Arg.substr(3));
			}
			else if (Arg == "/fp:fast")
			{
				Result.push_back("-ffast-math");
			}
		}
	}
	else
	{
		for (std::size_t I = 1; I < Expanded.size(); ++I)
		{
			const std::string& Arg = Expanded[I];
			if (Arg == "-c" || Arg == "-fsyntax-only")
			{
				continue;
			}
			if (Arg == "-o" || Arg == "-MF" || Arg == "-MT" || Arg == "-MQ" || Arg == "-include-pch")
			{
				++I;
				continue;
			}
			if (Arg == "-MD" || Arg == "-MMD" || Arg == "-MP")
			{
				continue;
			}
			if (CursorSource::Key(Directory / Path::PathFromUtf8(Arg)) == CursorSource::Key(Unit))
			{
				continue;
			}
			Result.push_back(Arg);
		}
	}
	Result.insert(Result.end(), Model.Input.AnalysisArguments.begin(), Model.Input.AnalysisArguments.end());
	Result.push_back(Path::PathToUtf8(Unit));
	std::string Fingerprint;
	for (const auto& Arg : Result)
	{
		Fingerprint += GenerationPaths::NormalizeInput(Model.Input, Arg) + '\n';
	}
	Model.Commands.push_back(std::move(Fingerprint));
	return Result;
}
} // namespace Mod::HeaderTool
