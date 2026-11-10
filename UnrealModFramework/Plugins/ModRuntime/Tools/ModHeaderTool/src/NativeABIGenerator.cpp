// Copyright (c) 2026 Nelaric Contributors
#include "NativeABIGenerator.h"

#include "Hash.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace Mod::HeaderTool
{
namespace
{
using FJson = nlohmann::json;

std::string Literal(const std::string& Value)
{
	return FJson(Value).dump();
}

std::string Upper(std::string Value)
{
	for (char& C : Value)
	{
		C = static_cast<char>(std::toupper(static_cast<unsigned char>(C)));
	}
	return Value;
}

std::string Source(const std::string& File)
{
	return "// Copyright (c) 2026 Nelaric Contributors\n/** @file " + File + " */\n";
}

std::string Scalar(const FAPIType& Type)
{
	if (Type.Kind == EAPITypeKind::Object)
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

bool IsBlueprintScalar(const FAPIType& Type)
{
	return Type.Public == "bool" || Type.Public == "float" || Type.Public == "double" || Type.Public == "int32" ||
	       Type.Public == "int64" || Type.Public == "uint8";
}

std::string Parameters(const FAPI& API, bool bNames)
{
	std::string Result = "uint64_t" + std::string(bNames ? " Context" : "") + ", uint64_t" + (bNames ? " Target" : "");
	for (std::size_t I = 0; I < API.Parameters.size(); ++I)
	{
		const auto& Type = API.Parameters[I].Type;
		const auto Name = "Arg" + std::to_string(I);
		if (Type.Kind == EAPITypeKind::ObjectInput)
		{
			Result += std::string(", const char*") + (bNames ? " " + Name : "") + ", uint32_t" +
			          (bNames ? " " + Name + "Size" : "");
		}
		else
		{
			Result += ", " + Scalar(Type) + (bNames ? " " + Name : "");
		}
	}
	if (API.Return.Kind != EAPITypeKind::Void)
	{
		Result += ", " + Scalar(API.Return) + "*" + (bNames ? " OutValue" : "");
	}
	return Result;
}

std::string Arguments(const FAPI& API, bool bSource)
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
		if (Type.Kind == EAPITypeKind::ObjectInput)
		{
			Result +=
			    bSource ? "static_cast<" + Type.Cpp + ">(" + Arg + "Object.GetValue())" : Arg + ", " + Arg + "Size";
		}
		else
		{
			Result += bSource ? "static_cast<" + Type.Cpp + ">(" + Arg + ")" : Arg;
		}
	}
	return Result + (!bSource && API.Return.Kind != EAPITypeKind::Void ? ", OutValue" : "");
}

std::string Member(const FAPI& API)
{
	std::string Result = API.Return.Cpp + " (" + API.DeclaringType + "::*)(";
	for (std::size_t I = 0; I < API.Parameters.size(); ++I)
	{
		Result += (I ? ", " : "") + API.Parameters[I].Type.Cpp;
	}
	return Result + ")" + (API.bConst ? " const" : "") + (API.bNoexcept ? " noexcept" : "");
}

const std::vector<std::string> Errors = {
    "None",          "WrongThread",      "BindingExpired", "NotRegistered",  "SignatureMismatch", "ObjectUnavailable",
    "ScopeMismatch", "PermissionDenied", "ReadOnly",       "InvalidArgument"};
} // namespace

FJson WriteNativeABI(const FGenerationModel& Model, std::map<std::string, std::string>& Files,
                     const std::string& ContractHash, const std::string& ProjectionHash, const FJson& Lifecycle)
{
	const std::string Game = Model.Input.GameId;
	const std::string Prefix = "UMF_" + Game;
	const std::string Macro = "UE_UMF_" + Upper(Game);
	const std::string ClientOwner = Game + "ModClient";
	if (std::any_of(Model.Input.Modules.begin(), Model.Input.Modules.end(),
	                [&](const auto& Module) { return Upper(Module.Name) == Upper(ClientOwner); }))
	{
		throw std::runtime_error("The reserved client owner conflicts with a host module: " + ClientOwner);
	}
	const std::string HeaderPath = "SDK/Public/" + Game + ".modabi.h";
	FJson APIs = FJson::array(), Required = FJson::array(), Connections = FJson::array();
	for (const FAPI& API : Model.APIs)
	{
		const std::string Symbol = Prefix + "_" + API.Module + "_" + API.Symbol;
		FJson Params = FJson::array({"uint64_t", "uint64_t"});
		for (const auto& Param : API.Parameters)
		{
			if (Param.Type.Kind == EAPITypeKind::ObjectInput)
			{
				Params.push_back("const char*");
				Params.push_back("uint32_t");
			}
			else
			{
				Params.push_back(Scalar(Param.Type));
			}
		}
		if (API.Return.Kind != EAPITypeKind::Void)
		{
			Params.push_back(Scalar(API.Return) + "*");
		}
		const FJson Signature{{"callingConvention", "cdecl"}, {"returns", "uint32_t"}, {"parameters", Params}};
		const std::string SignatureHash = HashBytes(Signature.dump());
		const std::string APIProjection = HashBytes(API.SourceSignature + Signature.dump() + ":ContextHandlesV1");
		APIs.push_back({{"id", API.Id},
		                {"version", API.Version},
		                {"ownerModule", API.Module},
		                {"symbol", Symbol},
		                {"hostSymbol", Symbol + "_Host"},
		                {"slot", API.Symbol},
		                {"signature", Signature},
		                {"signatureHash", SignatureHash},
		                {"projectionHash", APIProjection}});
		for (const auto& Owner : {API.Module, ClientOwner})
		{
			Required.push_back({{"ownerModule", Owner},
			                    {"symbol", Symbol + (Owner == API.Module ? "_Host" : "")},
			                    {"exportId", API.Id},
			                    {"signatureHash", SignatureHash},
			                    {"projectionHash", APIProjection}});
		}
	}
	FJson StatusCodes = FJson::object();
	for (std::size_t I = 0; I < Errors.size(); ++I)
	{
		StatusCodes[Errors[I]] = I;
	}
	StatusCodes["NotConnected"] = 65536;
	StatusCodes["ABIMismatch"] = 65537;
	StatusCodes["InternalError"] = 65538;
	for (const auto& Module : Model.Input.Modules)
	{
		Connections.push_back({{"ownerModule", Module.Name},
		                       {"tableType", Prefix + "_" + Module.Name + "_FHostAPIV1"},
		                       {"querySymbol", Prefix + "_" + Module.Name + "_GetHostAPIV1"},
		                       {"connectSymbol", Prefix + "_" + Module.Name + "_Connect"},
		                       {"disconnectSymbol", Prefix + "_" + Module.Name + "_Disconnect"},
		                       {"releaseObjectSymbol", Prefix + "_" + Module.Name + "_ReleaseObject"}});
	}
	FJson Descriptor{{"format", "UMF.ClientABI"},
	                 {"schemaVersion", 2},
	                 {"status", "Complete"},
	                 {"adapterABI", "ContextHandlesV1"},
	                 {"abiVersion", 1},
	                 {"gameId", Game},
	                 {"callingConvention", "cdecl"},
	                 {"tablePacking", 8},
	                 {"pointerBits", 64},
	                 {"boolean", "uint8_t:0-or-1"},
	                 {"outputRule", "ZeroOnFailureCommitOnSuccess"},
	                 {"thread", "GameThread"},
	                 {"header", HeaderPath},
	                 {"versionConstant", Macro + "_ABI_VERSION"},
	                 {"hashConstant", Macro + "_ABI_HASH"},
	                 {"clientOwnerModule", ClientOwner},
	                 {"clientAnchor", "UMF_ModClient_" + Game},
	                 {"statusCodes", StatusCodes},
	                 {"connections", Connections},
	                 {"apis", APIs}};
	Descriptor["contextDelivery"] = {{"issuer", "Mod::API::FNativeAccess::Open"},
	                                 {"scopeToken", "HostIssuedCallerScope"},
	                                 {"objectToken", "ContextOwnedNonOwning"},
	                                 {"revocation", "CallerScopeOrRegistryRevocation"},
	                                 {"releaseRule", "ReleaseInvalidatesAllCopiesOfTheSameToken"},
	                                 {"unloadRule", "DisconnectAndQuiesceBeforeHostModuleUnload"}};
	const std::string ABIHash = HashBytes(Descriptor.dump());
	Descriptor["nativeABIHash"] = ABIHash;
	Descriptor["contractHash"] = ContractHash;
	Descriptor["projectionHash"] = ProjectionHash;

	std::ostringstream Header, Client;
	Header << Source(Game + ".modabi.h") << "#pragma once\n\n#include <stdint.h>\n\n"
	       << "#define " << Macro << "_ABI_VERSION 1u\n#define " << Macro << "_ABI_HASH " << Literal(ABIHash) << "\n\n"
	       << "#if defined(_MSC_VER)\n#define " << Macro << "_CALL __cdecl\n#else\n#define " << Macro
	       << "_CALL\n#endif\n"
	       << "#if defined(" << Macro << "_HOST_BUILD) && defined(_WIN32)\n#define " << Macro
	       << "_HOST_API __declspec(dllexport)\n"
	       << "#elif defined(" << Macro << "_HOST_BUILD) && defined(__GNUC__)\n#define " << Macro
	       << "_HOST_API __attribute__((visibility(\"default\")))\n#else\n#define " << Macro << "_HOST_API\n#endif\n\n"
	       << "#if defined(" << Macro << "_CLIENT_BUILD) && defined(_WIN32)\n#define " << Macro
	       << "_CLIENT_API __declspec(dllexport)\n"
	       << "#elif defined(" << Macro << "_CLIENT_BUILD) && defined(__GNUC__)\n#define " << Macro
	       << "_CLIENT_API __attribute__((visibility(\"default\")))\n#else\n#define " << Macro
	       << "_CLIENT_API\n#endif\n\n"
	       << "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n";
	Header << "enum " << Prefix << "_EStatus\n{\n";
	for (const auto& [Name, Value] : StatusCodes.items())
	{
		Header << "    " << Prefix << "_" << Name << " = " << Value << ",\n";
	}
	Header << "};\n\n#pragma pack(push, 8)\n";
	Client << Source(Game + ".modclient.cpp") << "#define " << Macro << "_CLIENT_BUILD 1\n#include \"" << Game
	       << ".modabi.h\"\n"
	       << "#include <atomic>\n#include <cstring>\n\nstatic_assert(sizeof(void*) == 8);\n"
	       << "extern \"C\" uint32_t " << Macro << "_CALL UMF_ModClient_" << Game << "(void) { return 1; }\n\n";
	for (const auto& Connection : Connections)
	{
		const std::string Owner = Connection.at("ownerModule");
		const std::string Table = Connection.at("tableType");
		const std::string ModulePrefix = Prefix + "_" + Owner;
		const std::string ReleaseType = ModulePrefix + "_FReleaseObject";
		Header << "typedef uint32_t (" << Macro << "_CALL *" << ReleaseType
		       << ")(uint64_t Context, uint64_t Object);\n";
		for (std::size_t I = 0; I < Model.APIs.size(); ++I)
		{
			const auto& API = Model.APIs[I];
			if (API.Module == Owner)
			{
				Header << "typedef uint32_t (" << Macro << "_CALL *" << APIs[I].at("symbol").get<std::string>()
				       << "_Fn)(" << Parameters(API, true) << ");\n";
			}
		}
		Header << "typedef struct " << Table << "\n{\n    uint32_t StructSize;\n    uint32_t ABIVersion;\n"
		       << "    char NativeABIHash[65];\n    " << ReleaseType << " ReleaseObject;\n";
		for (std::size_t I = 0; I < Model.APIs.size(); ++I)
		{
			if (Model.APIs[I].Module == Owner)
			{
				Header << "    " << APIs[I].at("symbol").get<std::string>() << "_Fn " << Model.APIs[I].Symbol << ";\n";
			}
		}
		Header << "} " << Table << ";\n\n"
		       << "/** @brief Queries a matching host table before connecting the client. */\n"
		       << Macro << "_HOST_API uint32_t " << Macro << "_CALL " << Connection.at("querySymbol").get<std::string>()
		       << "(uint32_t Version, uint32_t Size, const char* ABIHash, " << Table << "* OutAPI);\n"
		       << "/** @brief Binds checked callbacks; disconnect and quiesce before unload. */\n"
		       << Macro << "_CLIENT_API uint32_t " << Macro << "_CALL "
		       << Connection.at("connectSymbol").get<std::string>() << "(const " << Table << "* Host);\n"
		       << "/** @brief Clears callbacks; contexts are revoked separately by the host. */\n"
		       << Macro << "_CLIENT_API uint32_t " << Macro << "_CALL "
		       << Connection.at("disconnectSymbol").get<std::string>() << "(void);\n"
		       << "/** @brief Releases a token and invalidates its copies, not the UObject. */\n"
		       << Macro << "_CLIENT_API uint32_t " << Macro << "_CALL "
		       << Connection.at("releaseObjectSymbol").get<std::string>() << "(uint64_t Context, uint64_t Object);\n\n";

		std::ostringstream Host;
		Host << Source(Owner + ".modabi.cpp") << "#define " << Macro << "_HOST_BUILD 1\n#include \"" << Game
		     << ".modabi.h\"\n#include \"ModAPINative.h\"\n#include \"ModAPIHost.h\"\n#include \"" << Game
		     << ".modcalls.h\"\n#include <cstring>\n#include <limits>\n#include <type_traits>\n";
		std::set<std::string> Headers;
		for (const auto& [Name, Type] : Model.Types)
		{
			if (Type.Module == Owner)
			{
				Headers.insert(Type.HeaderInclude);
			}
		}
		for (const auto& API : Model.APIs)
		{
			if (API.Module == Owner)
			{
				for (const auto& Parameter : API.Parameters)
				{
					if (Parameter.Type.Kind == EAPITypeKind::ObjectInput)
					{
						Headers.insert(Parameter.Type.HostInclude);
					}
				}
			}
		}
		for (const auto& Path : Headers)
		{
			Host << "#include " << Literal(Path) << "\n";
		}
		Host << "\nstatic_assert(sizeof(void*) == 8 && sizeof(float) == 4 && sizeof(double) == 8);\n"
		     << "static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559);\n"
		     << "static uint32_t NativeStatus(Mod::API::EError Error)\n{\n    switch (Error)\n    {\n";
		for (const auto& Error : Errors)
		{
			Host << "    case Mod::API::EError::" << Error << ": return " << Prefix << "_" << Error << ";\n";
		}
		Host << "    default: return " << Prefix << "_InternalError;\n    }\n}\n"
		     << "extern \"C\" " << Upper(Owner) << "_API uint32_t UMF_ModHost_" << Owner
		     << "_RetainABI(void) { return 1; }\n"
		     << "static uint32_t " << Macro << "_CALL NativeRelease(uint64_t Context, uint64_t Object)\n"
		     << "{ return NativeStatus(Mod::API::FNativeAccess::Release(Context, Object).GetError()); }\n\n";
		Client << "namespace\n{\nstd::atomic<" << ReleaseType << "> " << Owner << "_Release{nullptr};\n";
		for (std::size_t I = 0; I < Model.APIs.size(); ++I)
		{
			const auto& API = Model.APIs[I];
			if (API.Module != Owner)
			{
				continue;
			}
			const std::string Symbol = APIs[I].at("symbol");
			Client << "std::atomic<" << Symbol << "_Fn> " << Owner << "_" << API.Symbol << "{nullptr};\n";
			Header << "/** @brief Calls " << API.Id << "; output is zero on failure. */\n"
			       << Macro << "_CLIENT_API uint32_t " << Macro << "_CALL " << Symbol << "(" << Parameters(API, true)
			       << ");\n";
			Host << "extern \"C\" " << Macro << "_HOST_API uint32_t " << Macro << "_CALL " << Symbol << "_Host("
			     << Parameters(API, true) << ")\n{\n";
			if (API.Return.Kind != EAPITypeKind::Void)
			{
				Host << "    if (!OutValue) { return " << Prefix << "_InvalidArgument; }\n    *OutValue = 0;\n";
			}
			Host << "#if !PLATFORM_EXCEPTIONS_DISABLED\n    try\n    {\n#endif\n";
			for (std::size_t P = 0; P < API.Parameters.size(); ++P)
			{
				if (API.Parameters[P].Type.Public == "bool")
				{
					Host << "    if (Arg" << P << " > 1) { return " << Prefix << "_InvalidArgument; }\n";
				}
			}
			Host << "    auto Binding = Mod::API::FNativeAccess::Resolve(Context, Target, Mod::API::TBindingTraits<"
			     << API.Owner << ">::TypeId(), Mod::API::TBindingTraits<" << API.Owner << ">::Signature);\n"
			     << "    if (!Binding.IsSuccess()) { return NativeStatus(Binding.GetError()); }\n";
			if (!API.Permissions.empty())
			{
				Host << "    static const FName Permissions[] = {";
				for (const auto& Permission : API.Permissions)
				{
					Host << "FName(TEXT(" << Literal(Permission) << ")),";
				}
				Host << "};\n";
			}
			Host << "    auto Object = Mod::API::FHostAccess::Enter(Binding.GetValue(), Mod::API::TBindingTraits<"
			     << API.Owner << ">::TypeId(), Mod::API::EScope::" << API.Scope << ", "
			     << (API.Permissions.empty() ? "TConstArrayView<FName>()" : "MakeArrayView(Permissions)") << ", "
			     << (API.bConst && !API.bFullViewMember ? "false" : "true") << ");\n"
			     << "    if (!Object.IsSuccess()) { return NativeStatus(Object.GetError()); }\n";
			for (std::size_t P = 0; P < API.Parameters.size(); ++P)
			{
				const auto& Type = API.Parameters[P].Type;
				if (Type.Kind == EAPITypeKind::ObjectInput)
				{
					Host << "    auto Arg" << P
					     << "Object = Mod::API::FHostAccess::ResolveObjectInput(Binding.GetValue(), " << Type.Object
					     << "::StaticClass(), Arg" << P << ", Arg" << P << "Size);\n"
					     << "    if (!Arg" << P << "Object.IsSuccess()) { return NativeStatus(Arg" << P
					     << "Object.GetError()); }\n";
				}
			}
			Host << "    using FMember = " << Member(API) << ";\n"
			     << "    constexpr FMember Function = static_cast<FMember>(&" << API.DeclaringType << "::" << API.Name
			     << ");\n"
			     << "    " << (API.Return.Kind == EAPITypeKind::Void ? "" : "auto Value = ") << "(CastChecked<"
			     << API.Owner << ">(Object.GetValue())->*Function)(" << Arguments(API, true) << ");\n";
			if (API.Return.Kind == EAPITypeKind::Object)
			{
				Host << "    auto Projected = Mod::API::FHostAccess::ProjectObject(Binding.GetValue(), const_cast<"
				     << API.Return.Object << "*>(Value), Mod::API::TBindingTraits<" << API.Return.Object
				     << ">::TypeId(), Mod::API::TBindingTraits<" << API.Return.Object << ">::Signature, "
				     << (API.Return.bConst ? "true" : "false") << ");\n"
				     << "    if (!Projected.IsSuccess()) { return NativeStatus(Projected.GetError()); }\n"
				     << "    auto Token = Mod::API::FNativeAccess::Publish(Context, MoveTemp(Projected.GetValue()));\n"
				     << "    if (!Token.IsSuccess()) { return NativeStatus(Token.GetError()); }\n    *OutValue = "
				        "Token.GetValue();\n";
			}
			else if (API.Return.Kind != EAPITypeKind::Void)
			{
				Host << "    *OutValue = static_cast<" << Scalar(API.Return) << ">(Value);\n";
			}
			Host << "    return 0;\n#if !PLATFORM_EXCEPTIONS_DISABLED\n    }\n    catch (...) { return " << Prefix
			     << "_InternalError; }\n#endif\n}\n\n";
		}
		Client << "}\n";
		Host << "extern \"C\" " << Macro << "_HOST_API uint32_t " << Macro << "_CALL "
		     << Connection.at("querySymbol").get<std::string>()
		     << "(uint32_t Version, uint32_t Size, const char* ABIHash, " << Table << "* OutAPI)\n{\n"
		     << "    if (!OutAPI || Size != sizeof(" << Table << ")) { return " << Prefix << "_InvalidArgument; }\n"
		     << "    *OutAPI = {};\n    if (Version != 1 || !ABIHash || std::strcmp(ABIHash, " << Literal(ABIHash)
		     << ") != 0) { return " << Prefix << "_ABIMismatch; }\n"
		     << "    OutAPI->StructSize = sizeof(" << Table << ");\n    OutAPI->ABIVersion = 1;\n"
		     << "    std::memcpy(OutAPI->NativeABIHash, " << Literal(ABIHash)
		     << ", 65);\n    OutAPI->ReleaseObject = &NativeRelease;\n";
		Client << "extern \"C\" uint32_t " << Macro << "_CALL " << Connection.at("connectSymbol").get<std::string>()
		       << "(const " << Table << "* Host)\n{\n    if (!Host || Host->StructSize != sizeof(" << Table
		       << ") || Host->ABIVersion != 1 || std::memcmp(Host->NativeABIHash, " << Literal(ABIHash)
		       << ", 65) != 0 || !Host->ReleaseObject";
		for (std::size_t I = 0; I < Model.APIs.size(); ++I)
		{
			if (Model.APIs[I].Module == Owner)
			{
				Host << "    OutAPI->" << Model.APIs[I].Symbol << " = &" << APIs[I].at("symbol").get<std::string>()
				     << "_Host;\n";
				Client << " || !Host->" << Model.APIs[I].Symbol;
			}
		}
		Host << "    return 0;\n}\n";
		Client << ") { return " << Prefix << "_ABIMismatch; }\n";
		for (const auto& API : Model.APIs)
		{
			if (API.Module == Owner)
			{
				Client << "    " << Owner << "_" << API.Symbol << ".store(Host->" << API.Symbol
				       << ", std::memory_order_release);\n";
			}
		}
		Client << "    " << Owner
		       << "_Release.store(Host->ReleaseObject, std::memory_order_release);\n    return 0;\n}\n"
		       << "extern \"C\" uint32_t " << Macro << "_CALL " << Connection.at("disconnectSymbol").get<std::string>()
		       << "(void)\n{\n";
		for (const auto& API : Model.APIs)
		{
			if (API.Module == Owner)
			{
				Client << "    " << Owner << "_" << API.Symbol << ".store(nullptr, std::memory_order_release);\n";
			}
		}
		Client << "    " << Owner << "_Release.store(nullptr, std::memory_order_release);\n    return 0;\n}\n"
		       << "extern \"C\" uint32_t " << Macro << "_CALL "
		       << Connection.at("releaseObjectSymbol").get<std::string>()
		       << "(uint64_t Context, uint64_t Object)\n{\n    auto Function = " << Owner
		       << "_Release.load(std::memory_order_acquire);\n"
		       << "    return Function ? Function(Context, Object) : " << Prefix << "_NotConnected;\n}\n";
		for (std::size_t I = 0; I < Model.APIs.size(); ++I)
		{
			const auto& API = Model.APIs[I];
			if (API.Module != Owner)
			{
				continue;
			}
			Client << "extern \"C\" uint32_t " << Macro << "_CALL " << APIs[I].at("symbol").get<std::string>() << "("
			       << Parameters(API, true) << ")\n{\n";
			if (API.Return.Kind != EAPITypeKind::Void)
			{
				Client << "    if (!OutValue) { return " << Prefix << "_InvalidArgument; }\n    *OutValue = 0;\n";
			}
			Client << "    auto Function = " << Owner << "_" << API.Symbol << ".load(std::memory_order_acquire);\n"
			       << "    return Function ? Function(" << Arguments(API, false) << ") : " << Prefix
			       << "_NotConnected;\n}\n";
		}
		Files["Host/" + Owner + "/" + Owner + ".modabi.cpp"] = Host.str();
		for (const auto& Key : {"querySymbol", "connectSymbol", "disconnectSymbol", "releaseObjectSymbol"})
		{
			Required.push_back({{"ownerModule", std::string(Key) == "querySymbol" ? Owner : ClientOwner},
			                    {"symbol", Connection.at(Key)}});
		}
	}
	Header << "#pragma pack(pop)\n\n/** @brief Returns the compiled client ABI version. */\n"
	       << Macro << "_CLIENT_API uint32_t " << Macro << "_CALL UMF_ModClient_" << Game
	       << "(void);\n\n#ifdef __cplusplus\n}\n#endif\n";
	Files[HeaderPath] = Header.str();
	Files["Client/" + ClientOwner + "/" + ClientOwner + ".modclient.cpp"] = Client.str();
	Files["Contract/" + Game + ".clientabi.json"] = Descriptor.dump(2) + "\n";

	FJson SdkAPIs = FJson::array(), SdkTypes = FJson::array(), Unsupported = FJson::array();
	std::set<std::string> DirectResults;
	std::map<std::string, const FAPI*> Names;
	for (const auto& API : Model.APIs)
	{
		if (API.Return.Kind == EAPITypeKind::Object && !API.Return.bConst && API.Parameters.empty() &&
		    API.Scope == "Player" && Model.Types.at(API.Owner).bLocalPlayerSubsystem)
		{
			DirectResults.insert(API.Return.Object);
		}
	}
	std::set<std::string> ViewTypes = DirectResults;
	for (const auto& [Name, Type] : Model.Types)
	{
		if (Type.bFullView)
		{
			ViewTypes.insert(Name);
		}
	}
	for (std::size_t Previous = 0; Previous != ViewTypes.size();)
	{
		Previous = ViewTypes.size();
		for (const auto& API : Model.APIs)
		{
			if (API.Return.Kind == EAPITypeKind::Object &&
			    (ViewTypes.contains(API.Owner) || Model.Types.at(API.Owner).bLocalPlayerSubsystem))
			{
				ViewTypes.insert(API.Return.Object);
			}
		}
	}
	for (const auto& Name : ViewTypes)
	{
		const auto& Type = Model.Types.at(Name);
		FJson Entry{{"id", Type.Identity.Id}, {"kind", "ObjectReference"}, {"cppName", "FModObject_" + Type.Symbol}};
		if (Type.bFullView)
		{
			Entry["view"] = "Full";
		}
		SdkTypes.push_back(std::move(Entry));
	}
	bool bGetter = false, bMethod = false, bTypedMethod = false;
	for (std::size_t I = 0; I < Model.APIs.size(); ++I)
	{
		const auto& API = Model.APIs[I];
		const bool Getter = API.Return.Kind == EAPITypeKind::Object && !API.Return.bConst && API.Parameters.empty() &&
		                    API.Scope == "Player" && Model.Types.at(API.Owner).bLocalPlayerSubsystem;
		const bool Method = API.Return.Public == "float" && API.Parameters.size() == 1 &&
		                    API.Parameters[0].Type.Public == "float" && DirectResults.contains(API.Owner);
		const bool bPlayerProvider = API.Scope == "Player" && Model.Types.at(API.Owner).bLocalPlayerSubsystem;
		const bool bCanRepresentInputs =
		    std::all_of(API.Parameters.begin(), API.Parameters.end(),
		                [](const auto& Parameter)
		                {
			                const auto& Type = Parameter.Type;
			                return Type.Kind == EAPITypeKind::Scalar ||
			                       (Type.Kind == EAPITypeKind::ObjectInput && !Type.EngineInclude.empty() &&
			                        Type.Object.find("::") == Type.Object.npos);
		                });
		const bool bTyped =
		    !Getter && !Method && bCanRepresentInputs && (ViewTypes.contains(API.Owner) || bPlayerProvider);
		if (!Getter && !Method && !bTyped)
		{
			Unsupported.push_back(API.Id);
			continue;
		}
		std::string Name = API.bFullViewMember ? "View_" + API.ExportName : API.ExportName;
		std::string Fold = Name;
		std::transform(Fold.begin(), Fold.end(), Fold.begin(), [](unsigned char C) { return std::tolower(C); });
		if (const auto [Other, bInserted] = Names.emplace(Fold, &API); !bInserted)
		{
			const auto& Previous = *Other->second;
			if (API.ExportName != API.Name || Previous.ExportName != Previous.Name)
			{
				const bool bCurrentIsAlias = API.ExportName != API.Name;
				const auto& Source = bCurrentIsAlias ? API : Previous;
				const auto& Conflict = bCurrentIsAlias ? Previous : API;
				throw std::runtime_error(Source.NameLocation + ": error: compatible SDK name '" + Name +
				                         "' conflicts with the export at " + Conflict.NameLocation + ".");
			}
			Name += "_" + API.Symbol;
			Fold = Name;
			std::transform(Fold.begin(), Fold.end(), Fold.begin(), [](unsigned char C) { return std::tolower(C); });
			if (const auto [Existing, bUnique] = Names.emplace(Fold, &API); !bUnique)
			{
				throw std::runtime_error(API.NameLocation + ": error: generated SDK name '" + Name +
				                         "' conflicts with the export at " + Existing->second->NameLocation + ".");
			}
		}
		FJson Entry{{"id", API.Id},
		            {"name", Name},
		            {"displayName", API.ExportName},
		            {"shape", Getter ? "ObjectGetter" : (Method ? "FloatMethod" : "TypedMethod")},
		            {"scope", API.Scope},
		            {"thread", "GameThread"},
		            {"failureRule", "ReturnError"},
		            {"permissions", API.Permissions},
		            {"signatureHash", APIs[I].at("signatureHash")},
		            {"projectionHash", APIs[I].at("projectionHash")},
		            {"native", {{"header", HeaderPath}, {"symbol", APIs[I].at("symbol")}}}};
		if (Getter)
		{
			bGetter = true;
			Entry["resultTypeId"] = Model.Types.at(API.Return.Object).Identity.Id;
			Entry["provider"] = "LocalPlayerSubsystem";
			Entry["nullResult"] = "Error";
		}
		else if (Method)
		{
			bMethod = true;
			Entry["targetTypeId"] = Model.Types.at(API.Owner).Identity.Id;
			// Names and IDs are derived once by MHT; MST does not infer them.
			Entry["parameterName"] = "Arg0";
			Entry["parameterId"] = API.Id + ":parameter:0";
		}
		else
		{
			bTypedMethod = true;
			if (bPlayerProvider)
			{
				Entry["provider"] = "LocalPlayerSubsystem";
			}
			else
			{
				Entry["targetTypeId"] = Model.Types.at(API.Owner).Identity.Id;
			}
			FJson Parameters = FJson::array();
			bool bBlueprint = API.Return.Kind != EAPITypeKind::Scalar || IsBlueprintScalar(API.Return);
			for (const auto& Parameter : API.Parameters)
			{
				const auto& Type = Parameter.Type;
				if (Type.Kind == EAPITypeKind::ObjectInput)
				{
					Parameters.push_back({{"kind", "ObjectInput"},
					                      {"class", Type.Object},
					                      {"const", Type.bConst},
					                      {"include", Type.EngineInclude},
					                      {"module", Type.EngineModule}});
				}
				else
				{
					Parameters.push_back({{"kind", "Scalar"}, {"cppType", Type.Public}});
					bBlueprint &= IsBlueprintScalar(Type);
				}
			}
			Entry["parameters"] = std::move(Parameters);
			Entry["blueprint"] = bBlueprint;
			if (API.Return.Kind == EAPITypeKind::Object)
			{
				Entry["result"] = {{"kind", "ObjectReference"},
				                   {"typeId", Model.Types.at(API.Return.Object).Identity.Id}};
			}
			else if (API.Return.Kind == EAPITypeKind::Scalar)
			{
				Entry["result"] = {{"kind", "Scalar"}, {"cppType", API.Return.Public}};
			}
			else
			{
				Entry["result"] = {{"kind", "Void"}};
			}
		}
		SdkAPIs.push_back(std::move(Entry));
	}
	FJson Sdk{{"format", "UMF.SdkContract"},
	          {"schemaVersion", 2},
	          {"adapterABI", "ContextHandlesV1"},
	          {"status", Unsupported.empty() && !ViewTypes.empty() && ((bGetter && bMethod) || bTypedMethod)
	                         ? "Complete"
	                         : "Unsupported"},
	          {"unresolvedTypeCount", 0},
	          {"gameId", Game},
	          {"contractHash", ContractHash},
	          {"projectionHash", ProjectionHash},
	          {"nativeABIHash", ABIHash},
	          {"types", SdkTypes},
	          {"apis", SdkAPIs},
	          {"unsupportedAPIs", Unsupported},
	          {"connectionDescriptor", "Contract/" + Game + ".clientabi.json"}};
	if (!Lifecycle.is_null())
	{
		Sdk["modLifecycle"] = Lifecycle;
	}
	Files["Contract/" + Game + ".sdkcontract.json"] = Sdk.dump(2) + "\n";
	return {{"nativeABIHash", ABIHash},
	        {"requiredSymbols", Required},
	        {"clientOwnerModule", ClientOwner},
	        {"descriptor", "Contract/" + Game + ".clientabi.json"},
	        {"sdkContract", "Contract/" + Game + ".sdkcontract.json"}};
}
} // namespace Mod::HeaderTool
