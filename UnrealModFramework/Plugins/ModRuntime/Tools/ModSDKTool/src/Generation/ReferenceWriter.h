// Copyright (c) 2026 Nelaric Contributors
/** @file ReferenceWriter.h */
#pragma once

#include <sstream>
#include <string>

namespace Mod::SdkTool
{
class ReferenceWriter
{
public:
	static void WriteReference(std::ostringstream& Header, const std::string& Name, const std::string& SecondName,
	                           const std::string& APIMacro);
};
} // namespace Mod::SdkTool
