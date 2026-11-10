// Copyright (c) 2026 Nelaric Contributors
#include "CppSignature.h"

#include "ModHeaderTool/src/Generation/APIParameter.h"
#include "ModHeaderTool/src/Generation/APIType.h"

#include <cstddef>
#include <string>

namespace Mod::HeaderTool
{
std::string CppSignature::ResultType(const API& API)
{
	return "TModResult<" + API.Return.Public + ">";
}

std::string CppSignature::Parameters(const API& API, bool bSource, bool bOriginalNames)
{
	std::string Result;
	for (std::size_t I = 0; I < API.Parameters.size(); ++I)
	{
		if (I)
		{
			Result += ", ";
		}
		Result +=
		    (bSource ? API.Parameters[I].Type.Cpp : API.Parameters[I].Type.Public) + " " +
		    (bOriginalNames && !API.Parameters[I].Name.empty() ? API.Parameters[I].Name : "Arg" + std::to_string(I));
	}
	return Result;
}

std::string CppSignature::Arguments(const API& API, bool bSource)
{
	std::string Result;
	for (std::size_t I = 0; I < API.Parameters.size(); ++I)
	{
		if (I)
		{
			Result += ", ";
		}
		const auto Argument = "Arg" + std::to_string(I);
		Result += bSource ? "static_cast<" + API.Parameters[I].Type.Cpp + ">(" + Argument + ")" : Argument;
	}
	return Result;
}

std::string CppSignature::PointerType(const API& API)
{
	std::string Result = API.Return.Cpp + " (" + API.DeclaringType + "::*)(";
	for (std::size_t I = 0; I < API.Parameters.size(); ++I)
	{
		if (I)
		{
			Result += ", ";
		}
		Result += API.Parameters[I].Type.Cpp;
	}
	return Result + ")" + (API.bConst ? " const" : "") + (API.bNoexcept ? " noexcept" : "");
}
} // namespace Mod::HeaderTool
