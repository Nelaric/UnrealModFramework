// Copyright (c) 2026 Nelaric Contributors
#include "UeHookActions.h"

#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "Common/Files/PathName.h"
#include "Common/Process/ProcessArguments.h"
#include "Common/Process/ResponseFile.h"
#include "ModBuildTool/src/Build/ContextDocument.h"

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Mod::BuildTool
{
namespace
{
std::vector<std::string> SplitMsvc(std::string_view Text)
{
	std::vector<std::string> Arguments;
	std::string Word;
	std::size_t Slashes = 0;
	bool bQuoted = false;
	bool bStarted = false;
	for (std::size_t Index = 0; Index <= Text.size(); ++Index)
	{
		const char Character = Index < Text.size() ? Text[Index] : ' ';
		if (Character == '\\')
		{
			++Slashes;
			bStarted = true;
			continue;
		}
		if (Character == '"')
		{
			Word.append(Slashes / 2, '\\');
			if (Slashes % 2 != 0)
			{
				Word.push_back('"');
			}
			else if (bQuoted && Index + 1 < Text.size() && Text[Index + 1] == '"')
			{
				Word.push_back('"');
				++Index;
			}
			else
			{
				bQuoted = !bQuoted;
			}
			bStarted = true;
		}
		else
		{
			Word.append(Slashes, '\\');
			if (!bQuoted && std::isspace(static_cast<unsigned char>(Character)))
			{
				if (bStarted)
				{
					Arguments.push_back(std::exchange(Word, {}));
					bStarted = false;
				}
			}
			else
			{
				Word.push_back(Character);
				bStarted = true;
			}
		}
		Slashes = 0;
	}
	if (bQuoted)
	{
		throw std::runtime_error("Unterminated quoted UBT argument.");
	}
	return Arguments;
}

std::vector<std::string> SplitGnuResponse(std::string_view Text)
{
	std::vector<std::string> Arguments;
	std::string Word;
	char Quote = '\0';
	bool bStarted = false;
	for (std::size_t Index = 0; Index < Text.size(); ++Index)
	{
		const char Character = Text[Index];
		if (Character == '\\' && Index + 1 < Text.size())
		{
			Word.push_back(Text[++Index]);
		}
		else if (Quote != '\0')
		{
			if (Character == Quote)
			{
				Quote = '\0';
			}
			else
			{
				Word.push_back(Character);
			}
		}
		else if (Character == '\'' || Character == '"')
		{
			Quote = Character;
		}
		else if (std::isspace(static_cast<unsigned char>(Character)))
		{
			if (bStarted)
			{
				Arguments.push_back(std::exchange(Word, {}));
				bStarted = false;
			}
			continue;
		}
		else
		{
			Word.push_back(Character);
		}
		bStarted = true;
	}
	if (Quote != '\0')
	{
		throw std::runtime_error("Unterminated quote in UBT response file.");
	}
	if (bStarted)
	{
		Arguments.push_back(std::move(Word));
	}
	return Arguments;
}

std::string ReadResponse(const fs::path& Response)
{
	std::string Text = File::ReadFile(Response);
	if (Text.starts_with("\xef\xbb\xbf"))
	{
		return Text.substr(3);
	}
	const bool bLittleEndian = Text.starts_with("\xff\xfe");
	if (!bLittleEndian && !Text.starts_with("\xfe\xff"))
	{
		return Text;
	}
	if (Text.size() % 2 != 0)
	{
		throw std::runtime_error("Invalid UTF-16 UBT response file: " + Path::PathToUtf8(Response));
	}
	std::u16string Wide;
	for (std::size_t Index = 2; Index < Text.size(); Index += 2)
	{
		const auto Low = static_cast<unsigned char>(Text[Index + (bLittleEndian ? 0 : 1)]);
		const auto High = static_cast<unsigned char>(Text[Index + (bLittleEndian ? 1 : 0)]);
		Wide.push_back(static_cast<char16_t>(Low | (High << 8)));
	}
	const auto Utf8 = fs::path(Wide).u8string();
	return {Utf8.begin(), Utf8.end()};
}

struct CompileInputs
{
	const UeHookSources& Sources;
	const fs::path& Stage;
	std::size_t& NextResponse;
	fs::path WorkingDirectory;
	bool bMsvc;
	std::vector<fs::path> Matches;

	void Replace(std::vector<std::string>& Arguments, unsigned Depth)
	{
		if (Depth > 16)
		{
			throw std::runtime_error("UBT response file nesting is too deep.");
		}
		for (auto& Argument : Arguments)
		{
			if (Argument.starts_with('@'))
			{
				const fs::path Response = Path::ResolvePath(WorkingDirectory, Argument.substr(1));
				const auto Text = ReadResponse(Response);
				auto Tokens = bMsvc ? SplitMsvc(Text) : SplitGnuResponse(Text);
				const auto Before = Matches.size();
				Replace(Tokens, Depth + 1);
				if (Matches.size() != Before)
				{
					const auto Copy = Stage / (std::to_string(NextResponse++) + ".rsp");
					ResponseFile::WriteResponseFile(Copy, Tokens, bMsvc ? "msvc" : "gnu");
					Argument = "@" + Path::PathToUtf8(Copy);
				}
				continue;
			}
			const auto Extension = Mod::Tools::PathName::LowercaseName(Path::PathFromUtf8(Argument).extension());
			if (Argument.starts_with('-') || (Extension != u8".cpp" && Extension != u8".cc" && Extension != u8".cxx"))
			{
				continue;
			}
			const std::string Prefix =
			    Argument.starts_with("/Tp") || Argument.starts_with("/Tc") ? Argument.substr(0, 3) : std::string();
			const auto Source = Sources.find(Path::ResolvePath(WorkingDirectory, Argument.substr(Prefix.size())));
			if (Source != Sources.end())
			{
				Matches.push_back(Source->first);
				Argument = Prefix + Path::PathToUtf8(Source->second.Copy);
			}
		}
	}
};

void RestoreDependencies(Json& Actions)
{
	std::map<int, const Json*> ById;
	std::vector<int> Links;
	for (const auto& Action : Actions)
	{
		const int Id = Action.at("Id").get<int>();
		ById.emplace(Id, &Action);
		if (Action.at("Type") == "Link")
		{
			Links.push_back(Id);
		}
	}
	for (auto& Action : Actions)
	{
		// UE exports predecessor IDs and links imported actions by matching file paths.
		auto Prerequisites = Action.value("PrerequisiteItems", std::vector<std::string>{});
		std::set<std::string> Seen(Prerequisites.begin(), Prerequisites.end());
		auto Predecessors = Action.at("PrerequisiteActions").get<std::vector<int>>();
		if (Action.at("Type") == "PostBuildStep")
		{
			// An up-to-date receipt removes the intervening WriteMetadata action from UBT's outdated graph.
			Predecessors.insert(Predecessors.end(), Links.begin(), Links.end());
		}
		for (const int Predecessor : Predecessors)
		{
			const auto& Outputs = ById.at(Predecessor)->at("ProducedItems");
			if (Outputs.empty())
			{
				throw std::runtime_error("A required UBT predecessor has no file outputs.");
			}
			for (const auto& Output : Outputs)
			{
				const auto& Path = Output.get_ref<const std::string&>();
				if (Seen.insert(Path).second)
				{
					Prerequisites.push_back(Path);
				}
			}
		}
		Action["PrerequisiteItems"] = std::move(Prerequisites);
	}
}
} // namespace

UeHookObjects UeHookActions::Prepare(Json& Graph, const UeHookSources& Sources, const fs::path& Stage)
{
	auto& Actions = Graph.at("Actions");
	RestoreDependencies(Actions);
	UeHookObjects Objects;
	if (Sources.empty())
	{
		return Objects;
	}
	std::size_t NextResponse = 0;
	for (auto& Action : Actions)
	{
		if (ContextDocument::Text(Action, "Type") != "Compile")
		{
			continue;
		}
		const auto Compiler = Mod::Tools::PathName::LowercaseName(
		    Path::PathFromUtf8(ContextDocument::Text(Action, "CommandPath")).stem());
		CompileInputs Inputs{Sources,
		                     Stage,
		                     NextResponse,
		                     Path::PathFromUtf8(ContextDocument::Text(Action, "WorkingDirectory")),
		                     Compiler == u8"cl" || Compiler == u8"cl-filter" || Compiler == u8"clang-cl",
		                     {}};
		auto Arguments = SplitMsvc(ContextDocument::Text(Action, "CommandArguments"));
		Inputs.Replace(Arguments, 0);
		if (Inputs.Matches.empty())
		{
			continue;
		}
		const auto& Source = Inputs.Matches.front();
		if (Inputs.Matches.size() != 1 || Objects.contains(Source))
		{
			throw std::runtime_error("Each generated Hook source requires one non-unity UBT compile action.");
		}
		std::vector<fs::path> Outputs;
		for (const auto& Output : Action.at("ProducedItems"))
		{
			auto Object = Path::PathFromUtf8(Output.get_ref<const std::string&>());
			const auto Extension = Mod::Tools::PathName::LowercaseName(Object.extension());
			if (Extension == u8".obj" || Extension == u8".o")
			{
				Outputs.push_back(std::move(Object));
			}
		}
		if (Outputs.size() != 1)
		{
			throw std::runtime_error("Expected one UBT object output for " + Path::PathToUtf8(Source) + ".");
		}
		Objects.emplace(Source, std::move(Outputs.front()));

		std::string Command;
		for (const auto& Argument : Arguments)
		{
			if (!Command.empty())
			{
				Command.push_back(' ');
			}
			Command += ProcessArguments::QuoteMsvc(Argument);
		}
		const auto& Unit = Sources.at(Source);
		Action["CommandArguments"] = std::move(Command);
		Action.at("CommandVersion").get_ref<std::string&>() += " HookSource=" + Unit.GeneratedHash;
		Action["bCanExecuteRemotely"] = false;
		Action["bCanExecuteInUBA"] = false;
		std::cerr << "Compiling Hook source " << Path::PathToUtf8(Source.filename()) << " from "
		          << Path::PathToUtf8(Unit.Copy) << '\n';
	}
	return Objects;
}
} // namespace Mod::BuildTool
