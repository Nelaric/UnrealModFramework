// Copyright (c) 2026 Nelaric Contributors
#include "UeTargetReceipt.h"

#include "Common/Files/Path.h"
#include "ModBuildTool/src/Build/ContextDocument.h"

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

fs::path UeTargetReceipt::UeProductPath(const fs::path& ProjectRoot, std::string_view Path)
{
	constexpr std::string_view ProjectPrefix = "$(ProjectDir)/";
	return Path.starts_with(ProjectPrefix) ? Path::ResolvePath(ProjectRoot, Path.substr(ProjectPrefix.size()))
	                                       : fs::path();
}

void UeTargetReceipt::RequireUeBinary(const Request& Request, const Json& TargetReceipt, const fs::path& ProjectRoot)
{
	const Json& Target = Request.Context.at("target");
	for (const auto& [ReceiptKey, ContextKey] : {std::pair{"TargetName", "name"},
	                                             {"TargetType", "type"},
	                                             {"Platform", "platform"},
	                                             {"Architecture", "architecture"},
	                                             {"Configuration", "configuration"}})
	{
		if (ContextDocument::Text(TargetReceipt, ReceiptKey) != ContextDocument::Text(Target, ContextKey))
		{
			throw std::runtime_error("UBT target receipt does not match the host context: " + std::string(ReceiptKey));
		}
	}
	const Json& Products = TargetReceipt.at("BuildProducts");
	const std::string Kind = ContextDocument::Text(Request.OwningBinary, "kind");
	const bool bOwnsProduct =
	    std::any_of(Products.begin(), Products.end(),
	                [&](const Json& Product)
	                {
		                return Product.value("Type", "") == Kind &&
		                       UeProductPath(ProjectRoot, ContextDocument::Text(Product, "Path")) == Request.BinaryPath;
	                });
	if (!bOwnsProduct)
	{
		throw std::runtime_error("UBT target receipt has no matching owning binary: " +
		                         Path::PathToUtf8(Request.BinaryPath));
	}
	if (Kind == "Executable")
	{
		if (ContextDocument::Text(Target, "linkType") == "Monolithic" &&
		    UeProductPath(ProjectRoot, ContextDocument::Text(TargetReceipt, "Launch")) == Request.BinaryPath)
		{
			return;
		}
	}
	else if (ContextDocument::Text(Target, "linkType") == "Modular")
	{
		for (const auto& Product : Products)
		{
			if (Product.value("Type", "") != "RequiredResource")
			{
				continue;
			}
			const auto Path = UeProductPath(ProjectRoot, ContextDocument::Text(Product, "Path"));
			if (Path.extension() == ".modules" && Path.parent_path() == Request.BinaryPath.parent_path())
			{
				const auto Modules = ContextDocument::ReadJson(Path).at("Modules");
				if (Modules.value(Request.Owner, "") == Path::PathToUtf8(Request.BinaryPath.filename()))
				{
					return;
				}
			}
		}
	}
	throw std::runtime_error("UBT binary ownership could not be verified for module: " + Request.Owner);
}
} // namespace Mod::BuildTool
