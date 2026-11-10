// Copyright (c) 2026 Nelaric Contributors
#include "AssemblyAnalysis.h"

#include "Common/Files/Path.h"
#include "ModSourceTool/src/Assembly/AssemblyCalls.h"
#include "ModSourceTool/src/Assembly/AssemblyCompiler.h"
#include "ModSourceTool/src/Assembly/AssemblyEvidence.h"
#include "ModSourceTool/src/Assembly/AssemblyListing.h"
#include "ModSourceTool/src/Assembly/DebugLocations.h"
#include "ModSourceTool/src/Assembly/Instruction.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

void AssemblyAnalysis::AnalyzeAssembly(const SourceRequest& Request, const fs::path& Compiler,
                                       const fs::path& Directory, const std::vector<std::string>& Arguments,
                                       Json& Report)
{
	const auto Symbol = Report.at("assemblySymbol").get<std::string>();
	const auto Flag = Report.at("garbageFlag").get<std::uint64_t>();
	Report.erase("assemblySymbol");
	Report.erase("garbageFlag");
	if (!Request.bAnalyzeAssembly)
	{
		return;
	}
	auto& Failures = Report.at("failures");
	std::map<unsigned, unsigned> CallsByLine;
	bool bCallPath = false;
	for (const auto& Failure : Failures)
	{
		if (Failure.contains("validityCall"))
		{
			++CallsByLine[Failure.at("conditionLocation").at("line").get<unsigned>()];
		}
		bCallPath |= !Failure.at("callChain").empty() || Failure.value("exitKind", "") != "return";
	}
	if (CallsByLine.empty() && !bCallPath)
	{
		return;
	}
	const auto Driver = Compiler.filename();
	const auto Dialect = Driver == "cl.exe"         ? AssemblyCompiler::EDialect::Msvc
	                     : Driver == "clang-cl.exe" ? AssemblyCompiler::EDialect::ClangCL
	                                                : AssemblyCompiler::EDialect::Gnu;
	const bool bSupported =
	    (Dialect != AssemblyCompiler::EDialect::Msvc || Compiler.parent_path().filename() == "x64") &&
	    std::none_of(Arguments.begin(), Arguments.end(), [](const auto& Argument)
	                 { return Argument == "/GL" || Argument == "-m32" || Argument.starts_with("-flto"); });
	const auto Listing =
	    bSupported ? AssemblyCompiler::CompileListing(Compiler, Directory, Arguments, Dialect) : std::string{};
	const auto Source = Mod::BuildTool::Path::PathToUtf8(Request.Source);
	const auto Locations = Dialect == AssemblyCompiler::EDialect::Msvc
	                           ? DebugLocations{}
	                           : AssemblyListing::ReadDebugLocations(Listing, Directory);
	const auto Body = !bSupported ? std::vector<Instruction>{}
	                  : Dialect == AssemblyCompiler::EDialect::Msvc
	                      ? AssemblyListing::ReadProcedure(Listing, Symbol, Directory, Source)
	                      : AssemblyListing::ReadGasProcedure(Listing, Symbol, Source, Locations);
	for (auto& Failure : Failures)
	{
		if (Failure.contains("validityCall"))
		{
			const auto Line = Failure.at("conditionLocation").at("line").get<unsigned>();
			if (bSupported && CallsByLine.at(Line) == 1)
			{
				if (Dialect == AssemblyCompiler::EDialect::Msvc)
				{
					AssemblyEvidence::AnalyzeValidity(Failure, Body, Source, Flag, CallsByLine);
				}
				else
				{
					AssemblyEvidence::AnalyzeGasValidity(Failure, Body, Locations, Flag);
				}
			}
			else
			{
				Failure.erase("validityCall");
				AssemblyEvidence::SetEvidence(
				    Failure,
				    bSupported ? Json{{"kind", "unresolved"}, {"reason", "Multiple validity calls on one source line."}}
				               : Json{{"kind", "unavailable"},
				                      {"reason", "This compile action cannot emit the required listing."}});
			}
		}
		else if (bSupported && Failure.contains("classifications") &&
		         (!Failure.at("callChain").empty() || Failure.value("exitKind", "") != "return"))
		{
			AssemblyCalls::AnalyzeCallPath(Failure, Body);
		}
	}
}
} // namespace Mod::SourceTool
