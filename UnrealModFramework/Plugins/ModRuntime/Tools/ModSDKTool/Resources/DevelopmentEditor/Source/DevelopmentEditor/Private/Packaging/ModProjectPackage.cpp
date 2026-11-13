// Copyright (c) 2026 Nelaric Contributors
#include "ModProjectPackage.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/MonitoredProcess.h"
#include "Misc/Paths.h"
#include "ModPackageBuilder.h"
#include "Modules/ModuleManifest.h"
#include "PackageMigrationContext.h"
#include "PluginDescriptor.h"
#include "ProjectDescriptor.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogModProjectPackaging, Log, All);

namespace Mod::Editor
{
namespace
{
struct FHostTarget
{
	FString ProjectFile;
	FString Name;
	FString Type;
	FString Configuration;
	FString BuildId;
	FString ModuleManifest;
	FString CoreLibrary;
	FString ToolchainArguments;
	TSet<FName> Modules;
	TSet<FName> Plugins;
};

bool ReadHostTarget(const FModPackageRequest& Request, FHostTarget& OutTarget)
{
	FString Text;
	TSharedPtr<FJsonObject> Receipt;
	const TSharedPtr<FJsonObject>* Version = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Products = nullptr;
	FString Project, Platform, Architecture;
	if (IFileManager::Get().FileSize(*Request.HostTargetReceipt) > 16 * 1024 * 1024 ||
	    !FFileHelper::LoadFileToString(Text, *Request.HostTargetReceipt) ||
	    !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Receipt) || !Receipt.IsValid() ||
	    !Receipt->TryGetStringField(TEXT("Project"), Project) ||
	    !Receipt->TryGetStringField(TEXT("TargetName"), OutTarget.Name) ||
	    !Receipt->TryGetStringField(TEXT("TargetType"), OutTarget.Type) ||
	    !Receipt->TryGetStringField(TEXT("Platform"), Platform) || Platform != TEXT("Win64") ||
	    !Receipt->TryGetStringField(TEXT("Architecture"), Architecture) || Architecture != TEXT("x64") ||
	    !Receipt->TryGetStringField(TEXT("Configuration"), OutTarget.Configuration) ||
	    !Receipt->TryGetObjectField(TEXT("Version"), Version) || !Version->IsValid() ||
	    !(*Version)->TryGetStringField(TEXT("BuildId"), OutTarget.BuildId) || OutTarget.BuildId.IsEmpty() ||
	    !Receipt->TryGetArrayField(TEXT("BuildProducts"), Products) ||
	    (OutTarget.Type != TEXT("Game") && OutTarget.Type != TEXT("Editor")) ||
	    (OutTarget.Configuration != TEXT("Development") && OutTarget.Configuration != TEXT("Shipping")))
	{
		return false;
	}
	const FString ReceiptDirectory = FPaths::GetPath(Request.HostTargetReceipt);
	OutTarget.ProjectFile = FPaths::ConvertRelativePathToFull(ReceiptDirectory, Project);
	if (Project.IsEmpty() || !FPaths::FileExists(OutTarget.ProjectFile) || OutTarget.ProjectFile.Contains(TEXT("\"")) ||
	    OutTarget.Name.IsEmpty() || OutTarget.Name.Contains(TEXT("\"")) || OutTarget.Name.Contains(TEXT("\n")) ||
	    OutTarget.Name.Contains(TEXT("\r")))
	{
		return false;
	}
	const FString ProjectDirectory = FPaths::GetPath(OutTarget.ProjectFile);
	for (const TSharedPtr<FJsonValue>& Product : *Products)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		FString Path;
		if (!Product->TryGetObject(Object) || !Object->IsValid() || !(*Object)->TryGetStringField(TEXT("Path"), Path) ||
		    !Path.EndsWith(TEXT(".modules")))
		{
			continue;
		}
		if (!Path.StartsWith(TEXT("$(EngineDir)/")) && !Path.StartsWith(TEXT("$(ProjectDir)/")))
		{
			continue;
		}
		Path.ReplaceInline(TEXT("$(EngineDir)"), *Request.HostEngineDirectory, ESearchCase::CaseSensitive);
		Path.ReplaceInline(TEXT("$(ProjectDir)"), *ProjectDirectory, ESearchCase::CaseSensitive);
		FModuleManifest Manifest;
		if (FModuleManifest::TryRead(Path, Manifest) && Manifest.BuildId == OutTarget.BuildId)
		{
			if (const FString* Core = Manifest.ModuleNameToFileName.Find(TEXT("Core")))
			{
				OutTarget.ModuleManifest = FPaths::GetCleanFilename(Path);
				OutTarget.CoreLibrary = *Core;
			}
			for (const TPair<FString, FString>& Module : Manifest.ModuleNameToFileName)
			{
				OutTarget.Modules.Add(FName(*Module.Key));
			}
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Plugins = nullptr;
	if (Receipt->TryGetArrayField(TEXT("BuildPlugins"), Plugins))
	{
		for (const TSharedPtr<FJsonValue>& Plugin : *Plugins)
		{
			FString Name;
			if (Plugin->TryGetString(Name))
			{
				OutTarget.Plugins.Add(FName(*Name));
			}
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Properties = nullptr;
	if (Receipt->TryGetArrayField(TEXT("AdditionalProperties"), Properties))
	{
		const FRegexPattern ValuePattern(TEXT("[A-Za-z0-9_.]+"));
		for (const TSharedPtr<FJsonValue>& Property : *Properties)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			FString Name, Value;
			if (!Property->TryGetObject(Object) || !Object->IsValid() ||
			    !(*Object)->TryGetStringField(TEXT("Name"), Name) ||
			    !(*Object)->TryGetStringField(TEXT("Value"), Value) ||
			    (Name != TEXT("UMF.Compiler") && Name != TEXT("UMF.CompilerVersion") &&
			     Name != TEXT("UMF.WindowsSDKVersion")))
			{
				continue;
			}
			FRegexMatcher Match(ValuePattern, Value);
			if (!Match.FindNext() || Match.GetMatchBeginning() != 0 || Match.GetMatchEnding() != Value.Len())
			{
				return false;
			}
			OutTarget.ToolchainArguments += FString::Printf(TEXT(" -%s=%s"), *Name.RightChop(4), *Value);
		}
	}
	return !OutTarget.ModuleManifest.IsEmpty() &&
	       OutTarget.CoreLibrary == FPaths::GetCleanFilename(OutTarget.CoreLibrary) &&
	       OutTarget.CoreLibrary.EndsWith(TEXT(".dll")) && OutTarget.Modules.Contains(TEXT("CoreUObject")) &&
	       OutTarget.Modules.Contains(TEXT("Engine")) && OutTarget.Modules.Contains(TEXT("ModLoader"));
}

bool CopyTree(const FString& Source, const FString& Destination)
{
	const FString FullSource = FPaths::ConvertRelativePathToFull(Source);
	const FString FullDestination = FPaths::ConvertRelativePathToFull(Destination);
	return !FPaths::IsSamePath(FullDestination, FullSource) && !FPaths::IsUnderDirectory(FullDestination, FullSource) &&
	       FPlatformFileManager::Get().GetPlatformFile().CopyDirectoryTree(*FullDestination, *FullSource, false);
}

bool StageModule(const FString& Source, const FString& Destination, const FString& Name)
{
	if (!FPaths::FileExists(FPaths::Combine(Source, Name + TEXT(".Build.cs"))) || !CopyTree(Source, Destination))
	{
		return false;
	}
	// The authoring game entry point registers process-wide target settings.
	const FRegexPattern PrimaryModule(TEXT(
	    R"pattern(\bIMPLEMENT_PRIMARY_GAME_MODULE\s*\(\s*([A-Za-z_][A-Za-z_0-9:]*)\s*,\s*([A-Za-z_][A-Za-z_0-9]*)\s*,\s*(?:"[^"\r\n]*"|[A-Za-z_][A-Za-z_0-9]*)\s*\))pattern"));
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Destination, TEXT("*.cpp"), true, false);
	for (const FString& File : Files)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			return false;
		}
		FRegexMatcher Match(PrimaryModule, Text);
		if (!Match.FindNext())
		{
			continue;
		}
		const int32 Start = Match.GetMatchBeginning();
		const int32 End = Match.GetMatchEnding();
		const FString Implementation = Match.GetCaptureGroup(1);
		if (Match.GetCaptureGroup(2) != Name || Match.FindNext())
		{
			return false;
		}
		const FString Replacement = FString::Printf(TEXT("IMPLEMENT_GAME_MODULE(%s, %s)"), *Implementation, *Name);
		Text = Text.Left(Start) + Replacement + Text.Mid(End);
		if (!FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			return false;
		}
	}
	return true;
}

bool StageModules(const FProjectDescriptor& Project, const FHostTarget& Host, const FString& PluginDirectory,
                  FPluginDescriptor& OutPlugin)
{
	const bool bGame = Host.Type == TEXT("Game");
	const EBuildTargetType TargetType = bGame ? EBuildTargetType::Game : EBuildTargetType::Editor;
	const EBuildConfiguration Configuration =
	    Host.Configuration == TEXT("Shipping") ? EBuildConfiguration::Shipping : EBuildConfiguration::Development;
	const FString Destination = FPaths::Combine(PluginDirectory, TEXT("Source"));
	auto Stage = [&](const FModuleDescriptor& Module, const FString& Source)
	{
		const FString Name = Module.Name.ToString();
		FRegexMatcher NamePattern(FRegexPattern(TEXT("[A-Za-z][A-Za-z0-9_]{0,63}")), Name);
		if (!NamePattern.FindNext() || NamePattern.GetMatchBeginning() != 0 ||
		    NamePattern.GetMatchEnding() != Name.Len() || Host.Modules.Contains(Module.Name) ||
		    OutPlugin.Modules.ContainsByPredicate([&Module](const FModuleDescriptor& Existing)
		                                          { return Existing.Name == Module.Name; }) ||
		    !StageModule(FPaths::Combine(Source, Name), FPaths::Combine(Destination, Name), Name))
		{
			UE_LOG(LogModProjectPackaging, Error, TEXT("Could not stage project module '%s' from '%s'."), *Name,
			       *Source);
			return false;
		}
		FModuleDescriptor& Staged = OutPlugin.Modules.Add_GetRef(Module);
		Staged.LoadingPhase = ELoadingPhase::None;
		return true;
	};
	auto IsRuntime = [&](const FModuleDescriptor& Module)
	{
		return (Module.Type == EHostType::Runtime || Module.Type == EHostType::RuntimeNoCommandlet ||
		        Module.Type == EHostType::RuntimeAndProgram || Module.Type == EHostType::CookedOnly) &&
		       Module.IsCompiledInConfiguration(TEXT("Win64"), Configuration, Host.Name, TargetType, !bGame, bGame);
	};
	for (const FPluginReferenceDescriptor& Reference : Project.Plugins)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindEnabledPlugin(Reference.Name);
		if (!Reference.bEnabled || !Plugin)
		{
			continue;
		}
		if (Host.Plugins.Contains(FName(*Reference.Name)))
		{
			OutPlugin.Plugins.Add(Reference);
			continue;
		}
		if (Plugin->GetLoadedFrom() != EPluginLoadedFrom::Project || Plugin->CanContainContent())
		{
			continue;
		}
		// The author SDK can declare runtime bindings alongside a mirror of a host-owned module.
		const FString Source = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Source"));
		bool bCopiedModule = false;
		for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
		{
			if (IsRuntime(Module) && !Host.Modules.Contains(Module.Name))
			{
				if (!Stage(Module, Source))
				{
					return false;
				}
				bCopiedModule = true;
			}
		}
		const FString ThirdParty = FPaths::Combine(Source, TEXT("ThirdParty"));
		if (bCopiedModule && FPaths::DirectoryExists(ThirdParty) &&
		    !CopyTree(ThirdParty, FPaths::Combine(Destination, TEXT("ThirdParty"))))
		{
			return false;
		}
	}
	if (Host.Plugins.Contains(TEXT("ModRuntime")) &&
	    !OutPlugin.Plugins.ContainsByPredicate([](const FPluginReferenceDescriptor& Reference)
	                                           { return Reference.Name == TEXT("ModRuntime"); }))
	{
		OutPlugin.Plugins.Emplace(TEXT("ModRuntime"), true);
	}
	const int32 Dependencies = OutPlugin.Modules.Num();
	for (const FModuleDescriptor& Module : Project.Modules)
	{
		if (IsRuntime(Module) && !Stage(Module, FPaths::ProjectDir() / TEXT("Source")))
		{
			return false;
		}
	}
	return OutPlugin.Modules.Num() > Dependencies && OutPlugin.Modules.Num() <= 64;
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
	const FString SourceBinaries = FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries/Win64"));
	const FString CookBinaries = FPaths::Combine(ProjectDirectory, TEXT("Binaries/Win64"));
	if (!Files.MakeDirectory(*CookBinaries, true))
	{
		return false;
	}
	for (const TCHAR* Pattern : {TEXT("*.dll"), TEXT("*.modules"), TEXT("*.target"), TEXT("*.exe")})
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

bool CollectCode(const FHostTarget& Host, const FPluginDescriptor& Plugin, const FString& Directory,
                 FProjectModStage& OutStage)
{
	const int32 CoreSuffix = Host.CoreLibrary.Find(TEXT("-Core"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (CoreSuffix == INDEX_NONE)
	{
		return false;
	}
	TArray<TSharedPtr<FJsonValue>> Modules;
	TSharedRef<FJsonObject> ModuleFiles = MakeShared<FJsonObject>();
	for (const FModuleDescriptor& Module : Plugin.Modules)
	{
		const FString Name = Module.Name.ToString();
		const FString FileName = Host.CoreLibrary.Left(CoreSuffix + 1) + Name + Host.CoreLibrary.Mid(CoreSuffix + 5);
		const FString File = FPaths::Combine(Directory, FileName);
		if (!FPaths::FileExists(File))
		{
			UE_LOG(LogModProjectPackaging, Error, TEXT("UBT did not produce the expected module DLL: %s"), *File);
			return false;
		}
		ModuleFiles->SetStringField(Name, FileName);
		OutStage.NativePayloads.Add(TEXT("Native/") + FileName, File);
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Name);
		Entry->SetStringField(TEXT("file"), FileName);
		Modules.Add(MakeShared<FJsonValueObject>(Entry));
	}
	TSharedRef<FJsonObject> Manifest = MakeShared<FJsonObject>();
	Manifest->SetStringField(TEXT("BuildId"), Host.BuildId);
	Manifest->SetObjectField(TEXT("Modules"), ModuleFiles);
	FString Text;
	FJsonSerializer::Serialize(Manifest, TJsonWriterFactory<>::Create(&Text));
	const FString ManifestFile = FPaths::Combine(Directory, Host.ModuleManifest);
	if (!FFileHelper::SaveStringToFile(Text, *ManifestFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return false;
	}
	OutStage.NativePayloads.Add(TEXT("Native/") + Host.ModuleManifest, ManifestFile);
	OutStage.NativeCode = MakeShared<FJsonObject>();
	OutStage.NativeCode->SetStringField(TEXT("architecture"), TEXT("x64"));
	OutStage.NativeCode->SetStringField(TEXT("configuration"), Host.Configuration);
	OutStage.NativeCode->SetStringField(TEXT("targetType"), Host.Type);
	OutStage.NativeCode->SetStringField(TEXT("target"), Host.Name);
	OutStage.NativeCode->SetStringField(TEXT("buildId"), Host.BuildId);
	OutStage.NativeCode->SetStringField(TEXT("manifest"), Host.ModuleManifest);
	OutStage.NativeCode->SetArrayField(TEXT("modules"), Modules);
	return true;
}
} // namespace

FString FindPackagingDotnet(const FString& EngineDirectory)
{
#if PLATFORM_WINDOWS
	const TCHAR* ExecutableName = TEXT("dotnet.exe");
	const TCHAR* HostDirectory = TEXT("/win-x64/");
#elif PLATFORM_MAC
	const TCHAR* ExecutableName = TEXT("dotnet");
#if PLATFORM_CPU_ARM_FAMILY
	const TCHAR* HostDirectory = TEXT("/mac-arm64/");
#else
	const TCHAR* HostDirectory = TEXT("/mac-x64/");
#endif
#else
	const TCHAR* ExecutableName = TEXT("dotnet");
#if PLATFORM_CPU_ARM_FAMILY
	const TCHAR* HostDirectory = TEXT("/linux-arm64/");
#else
	const TCHAR* HostDirectory = TEXT("/linux-x64/");
#endif
#endif
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

bool StageProjectMod(const FModPackageRequest& Request, const FString& WorkDirectory, double Deadline,
                     FProjectModStage& OutStage, FString& Error)
{
	const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject();
	FHostTarget Host;
	if (!Project || !ReadHostTarget(Request, Host))
	{
		Error =
		    TEXT("Supply a Win64 x64 modular host target receipt with ModLoader and its matching Engine directory.");
		return false;
	}
	const FString PluginDirectory = FPaths::Combine(WorkDirectory, TEXT("Code"), Request.PluginName);
	const FString PluginFile = FPaths::Combine(PluginDirectory, Request.PluginName + TEXT(".uplugin"));
	FPluginDescriptor CodePlugin;
	CodePlugin.bExplicitlyLoaded = true;
	if (!StageModules(*Project, Host, PluginDirectory, CodePlugin) || !CodePlugin.Save(PluginFile))
	{
		Error = TEXT("Could not stage the Runtime modules declared by the author project.");
		return false;
	}
	const FString Dotnet = FindPackagingDotnet(Request.HostEngineDirectory);
	const FString Ubt =
	    FPaths::Combine(Request.HostEngineDirectory, TEXT("Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll"));
	FString Arguments = FString::Printf(
	    TEXT("\"%s\" \"%s\" Win64 %s -Project=\"%s\" -Plugin=\"%s\" -Modular -NoHotReload -NoUBTMakefiles"), *Ubt,
	    *Host.Name, *Host.Configuration, *Host.ProjectFile, *PluginFile);
	// Selecting individual modules keeps UBT from writing the host target receipt.
	for (const FModuleDescriptor& Module : CodePlugin.Modules)
	{
		Arguments += TEXT(" -Module=") + Module.Name.ToString();
	}
	Arguments += Host.ToolchainArguments;
	UE_LOG(LogModProjectPackaging, Display, TEXT("Building the Runtime modules of %s for %s."),
	       *FPaths::GetProjectFilePath(), *Host.Name);
	if (Dotnet.IsEmpty() || !FPaths::FileExists(Ubt) || !RunPackagingProcess(Dotnet, Arguments, Deadline) ||
	    !CollectCode(Host, CodePlugin, FPaths::Combine(PluginDirectory, TEXT("Binaries/Win64")), OutStage))
	{
		Error = TEXT("Could not build the mod DLLs or write their matching .modules file.");
		return false;
	}
	OutStage.Configuration = Host.Configuration;
	if (!StageCookProject(Request, *Project, WorkDirectory, OutStage))
	{
		Error = TEXT("Could not prepare the project assets for DLC cooking.");
		return false;
	}
	return true;
}
} // namespace Mod::Editor
