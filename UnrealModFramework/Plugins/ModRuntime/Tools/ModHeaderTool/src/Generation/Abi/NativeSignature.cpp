// Copyright (c) 2026 Nelaric Contributors
#include "NativeSignature.h"

#include "ModHeaderTool/src/Generation/APIParameter.h"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace Mod::HeaderTool
{
std::string NativeSignature::Scalar(const APIType& Type)
{
	if (Type.Kind == APIType::EKind::Object)
	{
		return "uint64_t";
	}
	if (Type.Public == "bool")
	{
		return "uint8_t";
	}
	if (Type.Public == "float" || Type.Public == "double" || Type.Public == "void")
	{
		return Type.Public;
	}
	if (Type.Public.starts_with("int") || Type.Public.starts_with("uint"))
	{
		return Type.Public + "_t";
	}
	throw std::runtime_error("No fixed-width client ABI scalar for " + Type.Public);
}

bool NativeSignature::IsBlueprintScalar(const APIType& Type)
{
	return Type.Public == "bool" || Type.Public == "float" || Type.Public == "double" || Type.Public == "int32" ||
	       Type.Public == "int64" || Type.Public == "uint8";
}

std::string NativeSignature::Parameters(const API& API, bool bNames)
{
	std::string Result = "uint64_t" + std::string(bNames ? " Context" : "") + ", uint64_t" + (bNames ? " Target" : "");
	for (std::size_t I = 0; I < API.Parameters.size(); ++I)
	{
		const auto& Type = API.Parameters[I].Type;
		const auto Name = "Arg" + std::to_string(I);
		if (Type.Kind == APIType::EKind::ObjectInput)
		{
			Result += std::string(", const char*") + (bNames ? " " + Name : "") + ", uint32_t" +
			          (bNames ? " " + Name + "Size" : "");
		}
		else
		{
			Result += ", " + Scalar(Type) + (bNames ? " " + Name : "");
		}
	}
	if (API.Return.Kind != APIType::EKind::Void)
	{
		Result += ", " + Scalar(API.Return) + "*" + (bNames ? " OutValue" : "");
	}
	return Result;
}

std::string NativeSignature::Arguments(const API& API, bool bSource)
{
	std::string Result = bSource ? "" : "Context, Target";
	for (std::size_t I = 0; I < API.Parameters.size(); ++I)
	{
		if (!Result.empty())
		{
			Result += ", ";
		}
		const auto Arg = "Arg" + std::to_string(I);
		const auto& Type = API.Parameters[I].Type;
		if (Type.Kind == APIType::EKind::ObjectInput)
		{
			Result +=
			    bSource ? "static_cast<" + Type.Cpp + ">(" + Arg + "Object.GetValue())" : Arg + ", " + Arg + "Size";
		}
		else
		{
			Result += bSource ? "static_cast<" + Type.Cpp + ">(" + Arg + ")" : Arg;
		}
	}
	return Result + (!bSource && API.Return.Kind != APIType::EKind::Void ? ", OutValue" : "");
}

std::string NativeSignature::Member(const API& API)
{
	std::string Result = API.Return.Cpp + " (" + API.DeclaringType + "::*)(";
	for (std::size_t I = 0; I < API.Parameters.size(); ++I)
	{
		Result += (I ? ", " : "") + API.Parameters[I].Type.Cpp;
	}
	return Result + ")" + (API.bConst ? " const" : "") + (API.bNoexcept ? " noexcept" : "");
}
} // namespace Mod::HeaderTool
