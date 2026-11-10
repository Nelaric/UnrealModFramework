// Copyright (c) 2026 Nelaric Contributors
#include "TypedMethodWriter.h"

#include "ModSDKTool/src/Generation/CppLiteral.h"
#include "ModSDKTool/src/Manifest/JsonDocument.h"

#include <cstddef>
#include <map>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

namespace Mod::SdkTool
{
using Json = nlohmann::json;

std::string TypedMethodWriter::NativeScalar(const std::string& Type)
{
	if (Type == "float" || Type == "double")
	{
		return Type;
	}
	return Type == "bool" ? "std::uint8_t" : "std::" + Type + "_t";
}

void TypedMethodWriter::WriteTypedMethod(std::ostringstream& Header, std::ostringstream& Source, const Json& API,
                                         const std::map<std::string, std::string>& Types, const std::string& Context,
                                         const std::string& Outcome, const std::string& LibraryClass,
                                         const std::string& APIMacro, const std::string& Plugin,
                                         const WrapperErrors& Errors)
{
	const bool bProvider = API.contains("provider");
	std::string Signature = (bProvider ? Context : Types.at(JsonDocument::Text(API, "targetTypeId"))) + " Target";
	const std::string TargetToken = bProvider ? "GetNativePlayerScope()" : "GetNativeObject()";
	std::string NativeSignature = "std::uint64_t, std::uint64_t";
	std::string Arguments = "static_cast<std::uint64_t>(Target.GetNativeContext()), "
	                        "static_cast<std::uint64_t>(Target." +
	                        TargetToken + ")";
	std::ostringstream Prepare;
	std::size_t Index = 0;
	for (const auto& Parameter : API.at("parameters"))
	{
		const std::string Arg = "Arg" + std::to_string(Index++);
		if (Parameter.at("kind") == "ObjectInput")
		{
			Signature += ", " + std::string(Parameter.at("const").get<bool>() ? "const " : "") +
			             JsonDocument::Text(Parameter, "class") + "* " + Arg;
			NativeSignature += ", const char*, std::uint32_t";
			Prepare << "\tif (" << Arg << " && !IsValid(" << Arg << ")) { return; }\n"
			        << "\tconst FString " << Arg << "Path = " << Arg << " ? " << Arg << "->GetPathName() : FString();\n"
			        << "\tconst FTCHARToUTF8 " << Arg << "Utf8(*" << Arg << "Path, " << Arg << "Path.Len());\n";
			Arguments +=
			    ", " + Arg + " ? " + Arg + "Utf8.Get() : nullptr, static_cast<std::uint32_t>(" + Arg + "Utf8.Length())";
		}
		else
		{
			const auto Type = JsonDocument::Text(Parameter, "cppType");
			Signature += ", " + Type + " " + Arg;
			NativeSignature += ", " + NativeScalar(Type);
			Arguments += ", static_cast<" + NativeScalar(Type) + ">(" + Arg + ")";
		}
	}
	const auto& Result = API.at("result");
	const auto ResultKind = JsonDocument::Text(Result, "kind");
	const bool bObject = ResultKind == "ObjectReference";
	const bool bResult = ResultKind != "Void";
	std::string ResultType;
	std::string NativeResult;
	if (bResult)
	{
		ResultType = bObject ? Types.at(JsonDocument::Text(Result, "typeId")) : JsonDocument::Text(Result, "cppType");
		NativeResult = bObject ? "std::uint64_t" : NativeScalar(ResultType);
		Signature += ", " + ResultType + "& Value";
		NativeSignature += ", " + NativeResult + "*";
		Arguments += ", &NativeValue";
	}
	Signature += ", " + Outcome + "& Outcome, int64& ErrorCode";
	const auto Name = JsonDocument::Text(API, "name");
	const auto Symbol = JsonDocument::Text(API.at("native"), "symbol");
	Header << "\t" << CppLiteral::LineDoc << " Invokes the bound API on the game thread.\n";
	if (API.at("blueprint").get<bool>())
	{
		Header << "\tUFUNCTION(BlueprintCallable, Category = \"Mod|" << Plugin
		       << "\", meta = (DisplayName = " << CppLiteral::Literal(JsonDocument::Text(API, "displayName"))
		       << ", ExpandEnumAsExecs = \"Outcome\", Keywords = " << CppLiteral::Literal(JsonDocument::Text(API, "id"))
		       << "))\n";
	}
	Header << "\tstatic " << APIMacro << " void " << Name << "(" << Signature << ");\n\n";
	Source << "static_assert(std::is_same_v<decltype(&::" << Symbol << "), std::uint32_t (*)(" << NativeSignature
	       << ")>, \"Generated native client ABI does not match ContextHandlesV1\");\n\n"
	       << "void " << LibraryClass << "::" << Name << "(" << Signature << ")\n{\n\tOutcome = " << Outcome
	       << "::Failed;\n\tErrorCode = " << Errors.InvalidArgument << ";\n";
	if (bResult)
	{
		Source << "\tValue = {};\n";
	}
	Source << "\tif (!IsInGameThread()) { ErrorCode = " << Errors.WrongThread << "; return; }\n"
	       << "\tif (Target.GetNativeContext() == 0 || Target." << TargetToken << " == 0) { return; }\n"
	       << Prepare.str();
	if (bResult)
	{
		Source << "\t" << NativeResult << " NativeValue = {};\n";
	}
	Source << "\tErrorCode = static_cast<int64>(::" << Symbol << "(" << Arguments << "));\n"
	       << "\tif (ErrorCode != 0) { return; }\n";
	if (bObject)
	{
		Source << "\tif (NativeValue == 0) { ErrorCode = " << Errors.ObjectUnavailable << "; return; }\n"
		       << "\tValue = " << ResultType
		       << "::FromNative(Target.GetNativeContext(), static_cast<uint64>(NativeValue));\n";
	}
	else if (bResult)
	{
		Source << "\tValue = static_cast<" << ResultType << ">(NativeValue);\n";
	}
	Source << "\tOutcome = " << Outcome << "::Succeeded;\n}\n\n";
}
} // namespace Mod::SdkTool
