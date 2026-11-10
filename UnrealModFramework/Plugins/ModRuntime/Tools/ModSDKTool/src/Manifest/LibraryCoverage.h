// Copyright (c) 2026 Nelaric Contributors
/** @file LibraryCoverage.h */
#pragma once

#include "ModSDKTool/src/Build/SdkRequest.h"
#include "ModSDKTool/src/Manifest/Library.h"

#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

namespace Mod::SdkTool
{
using Json = nlohmann::json;

class LibraryCoverage
{
public:
	explicit LibraryCoverage(SdkRequest& InRequest);

	void Add(const Library& Job);
	void Finalize();

private:
	void CollectPublicArtifacts(const Library& Job);
	void CollectPublicHeader(const Json& Artifact);
	void CollectBindings(const Library& Job);
	void RegisterBindings(const Library& Job, const std::string& Profile, bool bClient);
	void ValidateBindings() const;

	SdkRequest& Request;
	std::map<std::string, Json> Headers;
	std::set<std::string> Outputs;
	std::map<std::string, std::set<std::string>> ClientBindings;
	std::set<std::string> ClientDestinations;
	std::map<std::string, std::set<std::string>> HostBindings;
	bool bContractDeclared = false;
	bool bHasHost = false;
	bool bHasEditorClient = false;
};
} // namespace Mod::SdkTool
