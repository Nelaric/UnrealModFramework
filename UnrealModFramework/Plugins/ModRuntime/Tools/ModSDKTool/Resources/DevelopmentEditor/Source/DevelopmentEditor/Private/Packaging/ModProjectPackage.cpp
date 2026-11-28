// Copyright (c) 2026 Nelaric Contributors
#include "ModProjectPackage.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IProjectManager.h"
#include "Misc/MonitoredProcess.h"
#include "Misc/Paths.h"
#include "ModNativeTarget.h"
#include "ModPackageBuilder.h"
#include "Modules/ModuleManager.h"
#include "PackageMigrationContext.h"
#include "PluginDescriptor.h"
#include "ProjectDescriptor.h"

DEFINE_LOG_CATEGORY_STATIC(LogModProjectPackaging, Log, All);

namespace Mod::Editor
{
namespace
{
bool CopyTree(const FString& Source, const FString& Destination)
{
	const FString FullSource = FPaths::ConvertRelativePathToFull(Source);
	const FString FullDestination = FPaths::ConvertRelativePathToFull(Destination);
	return !FPaths::IsSamePath(FullDestination, FullSource) && !FPaths::IsUnderDirectory(FullDestination, FullSource) &&
	       FPlatformFileManager::Get().GetPlatformFile().CopyDirectoryTree(*FullDestination, *FullSource, false);
}

bool MigrateProjectContent(const FString& Destination, TMap<FName, FString>& OutPackages)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.ScanPathsSynchronous({TEXT("/Game")});
	Registry.WaitForCompletion();
	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(TEXT("/Game"), Assets, true, true);
	TSet<FName> Packages;
	for (const FAssetData& Asset : Assets)
	{
		const FString Name = Asset.PackageName.ToString();
		if (!Asset.IsRedirector() && !Name.StartsWith(TEXT("/Game/__ExternalActors__/")) &&
		    !Name.StartsWith(TEXT("/Game/__ExternalObjects__/")))
		{
			Packages.Add(Asset.PackageName);
		}
	}
	if (Packages.IsEmpty())
	{
		return false;
	}
	IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	const FDelegateHandle Handle = Tools.GetOnPackageMigration().AddLambda(
	    [&OutPackages](UE::AssetTools::FPackageMigrationContext& Context)
	    {
		    using EStep = UE::AssetTools::FPackageMigrationContext::EPackageMigrationStep;
		    if (Context.GetCurrentStep() == EStep::InstancedPackagesSaved)
		    {
			    for (const auto& Package : Context.GetMigrationPackagesData())
			    {
				    if (FPaths::FileExists(Package.GetDestinationFilename()))
				    {
					    OutPackages.Add(FName(*Package.GetOriginalPackageName()), Package.GetInstancedPackageName());
				    }
			    }
		    }
	    });
	TArray<FName> Names = Packages.Array();
	Names.Sort(FNameLexicalLess());
	Tools.MigratePackages(Names, Destination, FMigrationOptions());
	Tools.GetOnPackageMigration().Remove(Handle);
	for (FName Package : Names)
	{
		if (!OutPackages.Contains(Package))
		{
			UE_LOG(LogModProjectPackaging, Error, TEXT("Could not migrate project package '%s'."), *Package.ToString());
			return false;
		}
	}
	return true;
}

bool StageCookProject(const FModPackageRequest& Request, const FProjectDescriptor& Project,
                      const FString& WorkDirectory, FProjectModStage& OutStage)
{
	const FString ProjectDirectory = FPaths::Combine(WorkDirectory, TEXT("Project"));
	const FString PluginDirectory = FPaths::Combine(ProjectDirectory, TEXT("Plugins"), Request.PluginName);
	const FString ContentDirectory = FPaths::Combine(PluginDirectory, TEXT("Content"));
	OutStage.CookProject = FPaths::Combine(ProjectDirectory, FPaths::GetCleanFilename(FPaths::GetProjectFilePath()));
	OutStage.CookPlugin = FPaths::Combine(PluginDirectory, Request.PluginName + TEXT(".uplugin"));
	FProjectDescriptor Staged = Project;
	Staged.Plugins.Emplace(Request.PluginName, true);
	FPluginDescriptor ContentPlugin;
	ContentPlugin.bCanContainContent = true;
	FText Error;
	IFileManager& Files = IFileManager::Get();
	if (!Files.MakeDirectory(*ContentDirectory, true) || !Staged.Save(OutStage.CookProject, Error) ||
	    !ContentPlugin.Save(OutStage.CookPlugin, Error))
	{
		UE_LOG(LogModProjectPackaging, Error, TEXT("Could not write the cook project: %s"), *Error.ToString());
		return false;
	}
	for (const TCHAR* Directory : {TEXT("Config"), TEXT("Source"), TEXT("Plugins")})
	{
		const FString Source = FPaths::Combine(FPaths::ProjectDir(), Directory);
		if (FPaths::DirectoryExists(Source) && !CopyTree(Source, FPaths::Combine(ProjectDirectory, Directory)))
		{
			return false;
		}
	}
	const FString BinarySubdirectory = FPaths::Combine(TEXT("Binaries"), FPlatformProcess::GetBinariesSubdirectory());
	const FString SourceBinaries = FPaths::Combine(FPaths::ProjectDir(), BinarySubdirectory);
	const FString CookBinaries = FPaths::Combine(ProjectDirectory, BinarySubdirectory);
	if (!Files.MakeDirectory(*CookBinaries, true))
	{
		return false;
	}
	for (const TCHAR* Pattern :
	     {TEXT("*.dll"), TEXT("*.so"), TEXT("*.dylib"), TEXT("*.modules"), TEXT("*.target"), TEXT("*.exe")})
	{
		TArray<FString> Binaries;
		Files.FindFiles(Binaries, *FPaths::Combine(SourceBinaries, Pattern), true, false);
		for (const FString& Binary : Binaries)
		{
			if (Files.Copy(*FPaths::Combine(CookBinaries, Binary), *FPaths::Combine(SourceBinaries, Binary)) != COPY_OK)
			{
				return false;
			}
		}
	}
	return MigrateProjectContent(ContentDirectory, OutStage.MigratedPackages);
}

} // namespace

FString FindPackagingDotnet(const FString& EngineDirectory)
{
#if PLATFORM_WINDOWS
	const TCHAR* ExecutableName = TEXT("dotnet.exe");
	const TCHAR* Platform = TEXT("win");
#elif PLATFORM_MAC
	const TCHAR* ExecutableName = TEXT("dotnet");
	const TCHAR* Platform = TEXT("mac");
#else
	const TCHAR* ExecutableName = TEXT("dotnet");
	const TCHAR* Platform = TEXT("linux");
#endif
	const FString HostDirectory = FString::Printf(TEXT("/%s-%s/"), Platform, Native::GetHostArchitecture());
	TArray<FString> DotnetPaths;
	IFileManager::Get().FindFilesRecursive(DotnetPaths,
	                                       *FPaths::Combine(EngineDirectory, TEXT("Binaries/ThirdParty/DotNet")),
	                                       ExecutableName, true, false);
	DotnetPaths.Sort();
	FString Dotnet;
	for (FString& Candidate : DotnetPaths)
	{
		FPaths::NormalizeFilename(Candidate);
		if (Candidate.Contains(HostDirectory))
		{
			Dotnet = FPaths::ConvertRelativePathToFull(Candidate);
		}
	}
	return Dotnet;
}

bool RunPackagingProcess(const FString& Executable, const FString& Arguments, double Deadline)
{
	TAtomic<int32> ExitCode{INDEX_NONE};
	FMonitoredProcess Process(Executable, Arguments, true);
	Process.OnOutput().BindLambda([](const FString& Line)
	                              { UE_LOG(LogModProjectPackaging, Display, TEXT("%s"), *Line); });
	Process.OnCompleted().BindLambda([&ExitCode](int32 Code) { ExitCode.Store(Code); });
	Process.OnCanceled().BindLambda([&ExitCode] { ExitCode.Store(1); });
	if (!Process.Launch())
	{
		return false;
	}
	bool bTimedOut = false;
	while (ExitCode.Load() == INDEX_NONE)
	{
		Process.Update();
		if (!bTimedOut && FPlatformTime::Seconds() > Deadline)
		{
			bTimedOut = true;
			Process.Cancel(true);
		}
		FPlatformProcess::Sleep(0.05f);
	}
	return !bTimedOut && ExitCode.Load() == 0;
}

bool StageProjectContent(const FModPackageRequest& Request, const FString& WorkDirectory, FProjectModStage& OutStage)
{
	const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject();
	return Project && StageCookProject(Request, *Project, WorkDirectory, OutStage);
}
} // namespace Mod::Editor
