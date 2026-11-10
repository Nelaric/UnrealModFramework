// Copyright (c) 2026 Nelaric Contributors
#include "AssemblyListing.h"

#include "Common/Files/Path.h"
#include "ModSourceTool/src/Assembly/InlineSite.h"
#include "ModSourceTool/src/Assembly/ListingText.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;

DebugLocations AssemblyListing::ReadDebugLocations(std::string_view Listing, const fs::path& Directory)
{
	DebugLocations Locations;
	while (!Listing.empty())
	{
		auto Text = ListingText::NextLine(Listing);
		const auto Directive = ListingText::Word(Text);
		if (Directive == ".cv_file" || Directive == ".file")
		{
			const auto Id = ListingText::Number(Text);
			auto File = Mod::BuildTool::Path::PathFromUtf8(ListingText::Quoted(Text));
			if (Directive == ".file" && ListingText::Trim(Text).starts_with('"'))
			{
				File /= Mod::BuildTool::Path::PathFromUtf8(ListingText::Quoted(Text));
			}
			if (Id != 0 && !File.empty())
			{
				Locations.Files[Id] = Mod::BuildTool::Path::PathToUtf8((Directory / File).lexically_normal());
			}
		}
		else if (Directive == ".cv_inline_site_id")
		{
			const auto Id = ListingText::Number(Text);
			if (ListingText::Word(Text) == "within")
			{
				auto& Site = Locations.Sites[Id];
				Site.Parent = ListingText::Number(Text);
				if (ListingText::Word(Text) == "inlined_at")
				{
					Site.CallerFile = ListingText::Number(Text);
					Site.CallerLine = ListingText::Number(Text);
				}
			}
		}
		else if (Directive == ".cv_inline_linetable")
		{
			auto& Site = Locations.Sites[ListingText::Number(Text)];
			Site.OriginFile = ListingText::Number(Text);
			Site.OriginLine = ListingText::Number(Text);
		}
	}
	return Locations;
}

std::vector<Instruction> AssemblyListing::ReadGasProcedure(std::string_view Listing, std::string_view Symbol,
                                                           const std::string& SourceFile,
                                                           const DebugLocations& Locations)
{
	std::vector<Instruction> Body;
	const std::string Label = std::string(Symbol) + ':';
	const std::string QuotedLabel = '"' + std::string(Symbol) + "\":";
	bool bFound = false;
	std::string File = SourceFile;
	unsigned Line = 0;
	unsigned Site = 0;
	while (!Listing.empty())
	{
		auto Text = ListingText::NextLine(Listing);
		Text = ListingText::Trim(Text.substr(0, Text.find('#')));
		if (!bFound)
		{
			bFound = Text == Label || Text == QuotedLabel;
			continue;
		}
		const auto Code = Text;
		const auto Operation = ListingText::Word(Text);
		if (Operation == ".seh_endproc" || Operation == ".cfi_endproc" || Operation == ".size" ||
		    Code.starts_with(".Lfunc_end"))
		{
			return Body;
		}
		if (Operation == ".cv_loc" || Operation == ".loc")
		{
			Site = Operation == ".cv_loc" ? ListingText::Number(Text) : 0;
			File = Locations.File(ListingText::Number(Text));
			Line = ListingText::Number(Text);
		}
		else if (Code.ends_with(':') && !Code.starts_with(".Ltmp") && !Code.starts_with(".Lfunc_begin") &&
		         !Code.starts_with(".LVL") && !Code.starts_with(".LBE"))
		{
			Body.push_back({File, Line, {}, Code, "label", Code.substr(0, Code.size() - 1), Site});
		}
		else if (!Operation.empty() && !Operation.starts_with('.') && !Code.ends_with(':'))
		{
			Body.push_back({File, Line, {}, Code, Operation, ListingText::Trim(Text), Site});
		}
	}
	throw std::runtime_error("Cannot locate the complete assembler procedure for " + std::string(Symbol) + '.');
}

std::vector<Instruction> AssemblyListing::ReadProcedure(std::string_view Listing, std::string_view Symbol,
                                                        const fs::path& Directory, const std::string& SourceFile)
{
	std::vector<Instruction> Body;
	std::string File = SourceFile;
	unsigned Line = 0;
	std::string_view Source;
	bool bInProcedure = false;
	while (!Listing.empty())
	{
		const auto End = Listing.find('\n');
		const auto Text = ListingText::Trim(Listing.substr(0, End));
		Listing.remove_prefix(End == Listing.npos ? Listing.size() : End + 1);
		const auto Code = ListingText::Trim(Text.substr(0, Text.find(';')));
		const auto [First, Rest] = ListingText::Split(Code, Code.find_first_of(" \t"));
		if (!bInProcedure)
		{
			bInProcedure = First == Symbol && Rest == "PROC";
			continue;
		}
		if (First == Symbol && Rest == "ENDP")
		{
			return Body;
		}
		if (Text.starts_with("; File "))
		{
			File = Mod::BuildTool::Path::PathToUtf8(
			    (Directory / Mod::BuildTool::Path::PathFromUtf8(ListingText::Trim(Text.substr(7)))).lexically_normal());
			Line = 0;
			Source = {};
		}
		else if (Text.starts_with(';'))
		{
			const auto [Number, Content] = ListingText::Split(Text.substr(1), Text.substr(1).find(':'));
			if (const auto Parsed = ListingText::Unsigned(Number))
			{
				Line = static_cast<unsigned>(*Parsed);
				Source = Content;
			}
		}
		else if (Code.ends_with(':'))
		{
			Body.push_back({File, Line, Source, Code, "label", Code.substr(0, Code.size() - 1)});
		}
		else if (!Code.empty())
		{
			Body.push_back({File, Line, Source, Code, First, Rest});
		}
	}
	throw std::runtime_error("Cannot locate the complete MSVC assembly procedure for " + std::string(Symbol) + '.');
}
} // namespace Mod::SourceTool
