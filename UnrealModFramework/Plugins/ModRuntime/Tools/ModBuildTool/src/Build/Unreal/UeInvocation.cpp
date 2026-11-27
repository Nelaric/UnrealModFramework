// Copyright (c) 2026 Nelaric Contributors
#include "UeInvocation.h"

#include "Common/Files/Path.h"
#include "Common/Process/Process.h"
#include "ModBuildTool/src/Build/ContextDocument.h"
#include "ModBuildTool/src/Build/Unreal/UeHookBuild.h"

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

fs::path UeInvocation::EngineDirectory(const fs::path& Location)
{
	constexpr std::string_view Ubt = "Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll";
	const fs::path Root = fs::canonical(Path::FileSystemPath(Location));
	if (fs::is_regular_file(Path::FileSystemPath(Root / Ubt)))
	{
		return Root;
	}
	if (fs::is_regular_file(Path::FileSystemPath(Root / "Engine" / Ubt)))
	{
		return Root / "Engine";
	}
	throw std::runtime_error("UnrealBuildTool is missing from the specified engine.");
}

fs::path UeInvocation::Dotnet(const fs::path& Engine, const std::map<std::string, std::string>& Environment)
{
	const auto System = Environment.find("UE_USE_SYSTEM_DOTNET");
	if (System != Environment.end() && System->second == "1")
	{
		const auto Root = Environment.find("DOTNET_ROOT");
		if (Root == Environment.end() || Root->second.empty())
		{
			throw std::runtime_error("UE_USE_SYSTEM_DOTNET=1 requires DOTNET_ROOT.");
		}
		const auto Dotnet = Path::PathFromUtf8(Root->second) /
#ifdef _WIN32
		                    "dotnet.exe";
#else
		                    "dotnet";
#endif
		if (!fs::is_regular_file(Path::FileSystemPath(Dotnet)))
		{
			throw std::runtime_error("The requested system .NET is missing under DOTNET_ROOT.");
		}
		return Dotnet;
	}
#if defined(_WIN32)
	constexpr std::string_view Host = "win";
	constexpr std::string_view Executable = "dotnet.exe";
#elif defined(__APPLE__)
	constexpr std::string_view Host = "mac";
	constexpr std::string_view Executable = "dotnet";
#else
	constexpr std::string_view Host = "linux";
	constexpr std::string_view Executable = "dotnet";
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
	constexpr std::string_view Architecture = "arm64";
#elif defined(_M_X64) || defined(__x86_64__)
	constexpr std::string_view Architecture = "x64";
#else
	throw std::runtime_error("Unsupported UBT build host architecture.");
	constexpr std::string_view Architecture = "";
#endif
	const fs::path Runtimes = Engine / "Binaries/ThirdParty/DotNet";
	const auto Find = [&](std::string_view Arch)
	{
		const std::string Runtime = std::string(Host) + "-" + std::string(Arch);
		fs::path Dotnet;
		if (!fs::is_directory(Path::FileSystemPath(Runtimes)))
		{
			return Dotnet;
		}
		for (const auto& Entry : fs::directory_iterator(Path::FileSystemPath(Runtimes)))
		{
			const fs::path Candidate = Runtimes / Entry.path().filename() / Runtime / Executable;
			if (Candidate > Dotnet && fs::is_regular_file(Path::FileSystemPath(Candidate)))
			{
				Dotnet = Candidate;
			}
		}
		return Dotnet;
	};
	fs::path Dotnet = Find(Architecture);
	if (Dotnet.empty() && Host == "win" && Architecture == "arm64")
	{
		Dotnet = Find("x64");
	}
	if (Dotnet.empty())
	{
		throw std::runtime_error("Cannot find the engine's bundled .NET for " + std::string(Host) + "-" +
		                         std::string(Architecture) + " under " + Path::PathToUtf8(Engine) + ".");
	}
	return Dotnet;
}

fs::path UeInvocation::Editor(const fs::path& Engine, const UeHookTarget& Target)
{
#if defined(_WIN32)
	constexpr std::string_view Host = "Win64";
	constexpr std::string_view Name = "UnrealEditor-Cmd.exe";
#elif defined(__APPLE__)
	constexpr std::string_view Host = "Mac";
	constexpr std::string_view Name = "UnrealEditor.app/Contents/MacOS/UnrealEditor";
#else
	constexpr std::string_view Host = "Linux";
	constexpr std::string_view Name = "UnrealEditor";
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
	constexpr std::string_view Architecture = "arm64";
#else
	constexpr std::string_view Architecture = "x64";
#endif
	if (Target.Platform != Host || Target.Architecture != Architecture || Target.Configuration != "Development")
	{
		throw std::runtime_error("Metadata and automation require a native Development Editor.");
	}
	const auto Path = Engine / "Binaries" / Host / Name;
	if (!fs::is_regular_file(Path))
	{
		throw std::runtime_error("The selected Editor SDK does not provide " + std::string(Name) + ".");
	}
	return Path;
}

fs::path UeInvocation::Receipt(const fs::path& Project, const UeHookTarget& Target)
{
	const auto Directory = Project.parent_path() / "Binaries" / Target.Platform;
	const std::string Stem = Target.Target + "-" + Target.Platform + "-" + Target.Configuration;
	for (const auto& Name : {Target.Target, Stem + Target.Architecture, Stem})
	{
		const auto Receipt = Directory / (Name + ".target");
		if (!fs::is_regular_file(Path::FileSystemPath(Receipt)))
		{
			continue;
		}
		const auto Record = ContextDocument::ReadJson(Receipt);
		if (Record.at("TargetName") == Target.Target && Record.at("Platform") == Target.Platform &&
		    Record.at("Configuration") == Target.Configuration && Record.at("Architecture") == Target.Architecture)
		{
			return Receipt;
		}
	}
	throw std::runtime_error("No UBT build receipt matches the requested target and architecture.");
}

void UeInvocation::Run(const fs::path& Executable, const std::vector<std::string>& Arguments, const fs::path& Directory,
                       const std::map<std::string, std::string>& Environment, std::string_view Stage)
{
	const auto Started = std::chrono::steady_clock::now();
	const int Exit = Process::RunProcess(Executable, Arguments, Directory, Environment);
	const auto Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Started).count();
	std::cerr << "UMF timing " << Stage << ' ' << Seconds << " s (exit " << Exit << ")\n";
	if (Exit != 0)
	{
		throw std::runtime_error(std::string(Stage) + " failed with exit code " + std::to_string(Exit) + ".");
	}
}
} // namespace Mod::BuildTool
