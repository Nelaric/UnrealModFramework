// Copyright (c) 2026 Nelaric Contributors
/** @file GenerationModel.h */
#pragma once

#include "ModHeaderTool/MacroScanner.h"
#include "ModHeaderTool/Project.h"

#include <map>
#include <set>

namespace Mod::HeaderTool
{
struct FModuleInput
{
	std::string Name;
	fs::path Root;
	std::vector<fs::path> TranslationUnits;
};

struct FTypeIdentity
{
	std::string CppName;
	std::string Id;
	std::string Scope;
};

struct FGenerationInput
{
	FProject Project;
	fs::path File;
	fs::path Database;
	fs::path IdentityFile;
	std::string GameId;
	std::string Target;
	std::string Platform;
	std::string Configuration;
	std::string Architecture;
	std::vector<FModuleInput> Modules;
	std::map<std::string, FTypeIdentity> Types;
	std::vector<std::string> AnalysisArguments;
	std::map<std::string, fs::path> PathRoots;
	std::map<std::string, fs::path> HostContexts;
};

enum class EApiTypeKind
{
	Void,
	Scalar,
	Object
};

struct FApiType
{
	EApiTypeKind Kind = EApiTypeKind::Void;
	std::string Cpp;
	std::string Public;
	std::string Object;
	bool bConst = false;
};

struct FApiParameter
{
	std::string Name;
	FApiType Type;
};

struct FApi
{
	FExportMacro Macro;
	std::string Module;
	std::string Owner;
	std::string Name;
	std::string Id;
	std::string Version;
	std::string Scope;
	std::vector<std::string> Permissions;
	FApiType Return;
	std::vector<FApiParameter> Parameters;
	std::string SourceSignature;
	std::string Symbol;
	bool bConst = false;
	bool bNoexcept = false;
};

struct FResolvedType
{
	FTypeIdentity Identity;
	std::string Module;
	fs::path Header;
	std::vector<std::size_t> Apis;
	std::string Symbol;
	std::string Signature;
	bool bLocalPlayerSubsystem = false;
};

struct FGenerationModel
{
	FGenerationInput Input;
	std::vector<FApi> Apis;
	std::map<std::string, FResolvedType> Types;
	std::set<fs::path> Dependencies;
	std::vector<std::string> Commands;
	std::string ClangVersion;
	std::vector<fs::path> ConfigRoots;
	std::map<std::string, std::map<std::string, std::string>> ContextMacros;
};

FGenerationInput LoadGenerationInput(const fs::path& File);
FGenerationModel Analyze(FGenerationInput Input, const fs::path& Options, const fs::path& ClangLibrary,
                         const fs::path& AnalysisDatabase);
void Emit(FGenerationModel Model, const fs::path& OutputDirectory);
std::string NormalizeInput(const FGenerationInput& Input, std::string Text);
} // namespace Mod::HeaderTool
