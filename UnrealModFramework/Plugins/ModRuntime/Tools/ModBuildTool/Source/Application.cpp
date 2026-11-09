// Copyright (c) 2026 Nelaric Contributors
#include "ModBuildTool/Application.h"

#include "Archive.h"
#include "Binary.h"
#include "Build.h"
#include "Hash.h"
#include "Version.h"

#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

namespace Mod::BuildTool
{
namespace
{
void PrintHelp()
{
	std::cout << "ModBuildTool " << ToolVersion << R"( - build generated host archives independently of UBT

Usage:
  ModBuildTool plan --context <context.json> --generated-manifest <manifest.json>
                   [--output <plan.json>]
  ModBuildTool build-host --context <context.json> --generated-manifest <manifest.json>
                         [--expected-input <sha256>] [--output <result.json>]
  ModBuildTool verify-host --context <context.json> --generated-manifest <manifest.json>
                          [--output <verification.json>]
  ModBuildTool finalize-host --context <context.json> --generated-manifest <manifest.json>
                            --link-receipt <link.json> [--output <linked.json>]
  ModBuildTool finalize-ue-target --contexts <contexts.json> --ue-target-receipt <target.target>
  ModBuildTool refresh-ue-watches --contexts <contexts.json>
  ModBuildTool hash-file --file <path>
  ModBuildTool hash-context --file <context.json>
  ModBuildTool inspect-archive --file <archive> --platform <Win64|Linux|Mac>
                              --architecture <x64|arm64> --anchor <symbol>
                              [--symbol <required-symbol>]...
  ModBuildTool inspect-binary --file <image> --platform <Win64|Linux|Mac>
                             --architecture <x64|arm64> --kind <Executable|DynamicLibrary>
  ModBuildTool --help | --version

Requires UMF.ModHostContext v1 and UMF.GeneratedArtifacts v1 or v2.
Generated v2 declares per-owner required symbols and public artifact roles.
The generated batch must be Complete, hash-checked, and match the exact context.
MHT scan reports are not generation manifests. A zero-export module still needs
a generated compilation unit defining its registration anchor.

Compiler and archiver paths, arguments, environment, and dependencies come from
the owning UE module. MBT never discovers an approximate host compiler setup.
Supported native archive formats: Win64 COFF, Linux ELF, macOS Mach-O;
architectures: x64 and arm64. LTO, PCH, unity, and C++ modules are not supported.
Library creation does not prove successful final game linkage or runtime binding.
finalize-host requires a v2 manifest, owningBinary, and a trusted successful link
record. It verifies identities and actual image bytes, not runtime behavior.

JSON results use stdout or --output; compiler diagnostics use stderr.
Exit codes: 0 = success, 1 = invalid input/build failure/stale verification.
)";
}

int Run(const std::vector<std::string>& Arguments)
{
	try
	{
		if (Arguments.size() == 1 || (Arguments.size() == 2 && (Arguments[1] == "--help" || Arguments[1] == "-h")))
		{
			PrintHelp();
			return 0;
		}
		if (Arguments.size() == 2 && Arguments[1] == "--version")
		{
			std::cout << "ModBuildTool " << ToolVersion << '\n';
			return 0;
		}
		const std::string Command = Arguments.at(1);
		if (Command != "plan" && Command != "build-host" && Command != "verify-host" && Command != "hash-file" &&
		    Command != "hash-context" && Command != "inspect-archive" && Command != "inspect-binary" &&
		    Command != "finalize-host" && Command != "finalize-ue-target" && Command != "refresh-ue-watches")
		{
			throw std::runtime_error("Unknown command '" + Command + "'. Use --help.");
		}
		std::map<std::string, std::string> Options;
		std::vector<std::string> ExtraSymbols;
		for (std::size_t Index = 2; Index < Arguments.size(); ++Index)
		{
			const std::string& Option = Arguments[Index];
			if (Command == "inspect-archive" && Option == "--symbol")
			{
				if (Index + 1 == Arguments.size() || Arguments[Index + 1].empty() ||
				    Arguments[Index + 1].starts_with("--"))
				{
					throw std::runtime_error("--symbol requires a symbol name.");
				}
				ExtraSymbols.push_back(Arguments[++Index]);
				continue;
			}
			const bool bAllowed =
			    Command == "inspect-archive"
			        ? Option == "--file" || Option == "--platform" || Option == "--architecture" || Option == "--anchor"
			    : Command == "inspect-binary"
			        ? Option == "--file" || Option == "--platform" || Option == "--architecture" || Option == "--kind"
			    : (Command == "hash-file" || Command == "hash-context") ? Option == "--file"
			    : Command == "finalize-ue-target" ? Option == "--contexts" || Option == "--ue-target-receipt"
			    : Command == "refresh-ue-watches"
			        ? Option == "--contexts"
			        : Option == "--context" || Option == "--generated-manifest" || Option == "--output" ||
			              (Command == "build-host" && Option == "--expected-input") ||
			              (Command == "finalize-host" && Option == "--link-receipt");
			if (!bAllowed || Options.contains(Option) || Index + 1 == Arguments.size() ||
			    Arguments[Index + 1].empty() || Arguments[Index + 1].starts_with("--"))
			{
				throw std::runtime_error("Unknown, duplicate, or missing-value option: " + Option);
			}
			Options.emplace(Option, Arguments[++Index]);
		}
		if (Command == "inspect-archive")
		{
			for (const char* Required : {"--file", "--platform", "--architecture", "--anchor"})
			{
				if (!Options.contains(Required))
				{
					throw std::runtime_error(std::string("inspect-archive requires ") + Required);
				}
			}
			const fs::path Archive = PathFromUtf8(Options.at("--file"));
			ExtraSymbols.insert(ExtraSymbols.begin(), Options.at("--anchor"));
			ValidateHostArchive(Archive, Options.at("--platform"), Options.at("--architecture"), ExtraSymbols);
			std::cout << FJson({{"format", "UMF.ArchiveInspection"},
			                    {"schemaVersion", 1},
			                    {"valid", true},
			                    {"requiredSymbols", ExtraSymbols},
			                    {"library", PathToUtf8(fs::canonical(Archive))},
			                    {"libraryHash", HashFile(Archive)}})
			                 .dump(2)
			          << '\n';
			return std::cout ? 0 : 1;
		}
		if (Command == "inspect-binary")
		{
			for (const char* Required : {"--file", "--platform", "--architecture", "--kind"})
			{
				if (!Options.contains(Required))
				{
					throw std::runtime_error(std::string("inspect-binary requires ") + Required);
				}
			}
			const fs::path Binary = PathFromUtf8(Options.at("--file"));
			ValidateHostBinary(Binary, Options.at("--platform"), Options.at("--architecture"), Options.at("--kind"));
			std::cout << FJson({{"format", "UMF.BinaryInspection"},
			                    {"schemaVersion", 1},
			                    {"valid", true},
			                    {"binary", PathToUtf8(fs::canonical(Binary))},
			                    {"contentHash", HashFile(Binary)}})
			                 .dump(2)
			          << '\n';
			return std::cout ? 0 : 1;
		}
		if (Command == "hash-file" || Command == "hash-context")
		{
			if (!Options.contains("--file"))
			{
				throw std::runtime_error(Command + " requires --file.");
			}
			const fs::path Input = PathFromUtf8(Options.at("--file"));
			std::cout << (Command == "hash-file" ? HashFile(Input) : HashHostContext(Input)) << '\n';
			return std::cout ? 0 : 1;
		}
		if (Command == "refresh-ue-watches")
		{
			if (!Options.contains("--contexts"))
			{
				throw std::runtime_error("refresh-ue-watches requires --contexts.");
			}
			RefreshUeWatches(PathFromUtf8(Options.at("--contexts")));
			return 0;
		}
		if (Command == "finalize-ue-target")
		{
			if (!Options.contains("--contexts") || !Options.contains("--ue-target-receipt"))
			{
				throw std::runtime_error("finalize-ue-target requires --contexts and --ue-target-receipt.");
			}
			std::cout << FinalizeUeTarget(PathFromUtf8(Options.at("--contexts")),
			                              PathFromUtf8(Options.at("--ue-target-receipt")))
			                 .dump(2)
			          << '\n';
			return std::cout ? 0 : 1;
		}
		if (!Options.contains("--context") || !Options.contains("--generated-manifest"))
		{
			throw std::runtime_error("Both --context and --generated-manifest are required.");
		}
		const fs::path Context = PathFromUtf8(Options.at("--context"));
		const fs::path Manifest = PathFromUtf8(Options.at("--generated-manifest"));
		const fs::path Output = Options.contains("--output") ? PathFromUtf8(Options.at("--output")) : fs::path();
		const std::string Expected = Options.contains("--expected-input") ? Options.at("--expected-input") : "";
		if (Command == "finalize-host" && !Options.contains("--link-receipt"))
		{
			throw std::runtime_error("finalize-host requires --link-receipt.");
		}
		const fs::path LinkReceipt =
		    Options.contains("--link-receipt") ? PathFromUtf8(Options.at("--link-receipt")) : fs::path();
		if (!Expected.empty())
		{
			RequireDigest(Expected, "--expected-input");
		}
		ValidateResultOutput(Output, Context, Manifest, LinkReceipt);
		const FJson Result = Command == "build-host"      ? BuildHost(Context, Manifest, Expected)
		                     : Command == "finalize-host" ? FinalizeHost(Context, Manifest, LinkReceipt)
		                                                  : PlanHostBuild(Context, Manifest);
		WriteResult(Result, Output, Context, Manifest, LinkReceipt);
		return Command == "verify-host" && !Result.at("upToDate").get<bool>() ? 1 : 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << "ModBuildTool: error: " << Error.what() << '\n';
		return 1;
	}
}
} // namespace

int RunApplication(int Count, char* Arguments[])
{
	return Run(std::vector<std::string>(Arguments, Arguments + Count));
}

#ifdef _WIN32
int RunApplication(int Count, wchar_t* Arguments[])
{
	std::vector<std::string> Utf8Arguments;
	for (int Index = 0; Index < Count; ++Index)
	{
		Utf8Arguments.push_back(PathToUtf8(fs::path(Arguments[Index])));
	}
	return Run(Utf8Arguments);
}
#endif
} // namespace Mod::BuildTool
