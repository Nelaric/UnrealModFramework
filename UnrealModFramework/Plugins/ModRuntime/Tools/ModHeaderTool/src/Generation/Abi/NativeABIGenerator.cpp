// Copyright (c) 2026 Nelaric Contributors
#include "NativeABIGenerator.h"

#include "Common/Errors/PublicStatuses.h"
#include "ModHeaderTool/src/Generation/API.h"
#include "ModHeaderTool/src/Generation/APIParameter.h"
#include "ModHeaderTool/src/Generation/APIType.h"
#include "ModHeaderTool/src/Generation/Abi/NativeCode.h"
#include "ModHeaderTool/src/Generation/Abi/NativeSignature.h"
#include "ModHeaderTool/src/Generation/GenerationInput.h"
#include "ModHeaderTool/src/Generation/ModuleInput.h"
#include "ModHeaderTool/src/Generation/ResolvedType.h"
#include "ModHeaderTool/src/Generation/TypeIdentity.h"
#include "ModHeaderTool/src/Hash/Hash.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iomanip>
#include <map>
#include <nlohmann/json.hpp>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace Mod::HeaderTool
{
using Json = nlohmann::json;

Json NativeABIGenerator::WriteNativeABI(const GenerationModel& Model, std::map<std::string, std::string>& Files,
                                        const std::string& ContractHash, const std::string& ProjectionHash,
                                        const Json& Lifecycle, const std::string& ErrorHeader)
{
	const std::string Game = Model.Input.GameId;
	const std::string Prefix = "UMF_" + Game;
	const std::string Macro = "UE_UMF_" + NativeCode::Upper(Game);
	const std::string ClientOwner = Game + "ModClient";
	if (std::any_of(Model.Input.Modules.begin(), Model.Input.Modules.end(), [&](const auto& Module)
	                { return NativeCode::Upper(Module.Name) == NativeCode::Upper(ClientOwner); }))
	{
		throw std::runtime_error("The reserved client owner conflicts with a host module: " + ClientOwner);
	}
	const std::string HeaderPath = "SDK/Public/" + Game + ".modabi.h";
	Json APIs = Json::array(), Required = Json::array(), Connections = Json::array();
	for (const API& API : Model.APIs)
	{
		const std::string Symbol = Prefix + "_" + API.Module + "_" + API.Symbol;
		Json Params = Json::array({"uint64_t", "uint64_t"});
		for (const auto& Param : API.Parameters)
		{
			if (Param.Type.Kind == APIType::EKind::ObjectInput)
			{
				Params.push_back("const char*");
				Params.push_back("uint32_t");
			}
			else
			{
				Params.push_back(NativeSignature::Scalar(Param.Type));
			}
		}
		if (API.Return.Kind != APIType::EKind::Void)
		{
			Params.push_back(NativeSignature::Scalar(API.Return) + "*");
		}
		const Json Signature{{"callingConvention", "cdecl"}, {"returns", "uint32_t"}, {"parameters", Params}};
		const std::string SignatureHash = Hash::HashBytes(Signature.dump());
		const std::string APIProjection = Hash::HashBytes(API.SourceSignature + Signature.dump() + ":ContextHandlesV1" +
		                                                  (API.Blueprint ? Json(API.Permissions).dump() : ""));
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
	Json StatusCodes = Json::object();
	std::map<std::string, const char*> StatusMacros;
	auto ErrorMacros = Model.SourceErrorMacros;
	const bool bStructuredErrors = !ErrorHeader.empty();
	const auto AddStatuses = [&](const auto& Statuses)
	{
		for (const auto& Status : Statuses)
		{
			StatusCodes[Status.Key] = bStructuredErrors ? Status.Code : Status.LegacyCode;
			if (bStructuredErrors)
			{
				StatusMacros.emplace(Status.Key, Status.Macro);
				const auto [Entry, bInserted] = ErrorMacros.emplace(Status.Macro, Status.Code);
				if (!bInserted && Entry->second != Status.Code)
				{
					throw std::runtime_error(std::string("Conflicting SDK error macro: ") + Status.Macro);
				}
			}
		}
	};
	AddStatuses(Tools::PublicStatuses::FrameworkStatuses);
	AddStatuses(Tools::PublicStatuses::AdapterStatuses);
	const std::string ErrorHeaderName = Game + ".moderrors.h";
	if (bStructuredErrors)
	{
		std::ostringstream Header;
		Header << NativeCode::Source(ErrorHeaderName) << "#pragma once\n\n"
		       << std::hex << std::uppercase << std::setfill('0');
		for (const auto& [MacroName, Code] : ErrorMacros)
		{
			Header << "#define " << MacroName << " 0x" << std::setw(8) << Code << "u\n";
		}
		Files[ErrorHeader] = Header.str();
	}
	for (const auto& Module : Model.Input.Modules)
	{
		Connections.push_back({{"ownerModule", Module.Name},
		                       {"tableType", Prefix + "_" + Module.Name + "_FHostAPIV1"},
		                       {"querySymbol", Prefix + "_" + Module.Name + "_GetHostAPIV1"},
		                       {"connectSymbol", Prefix + "_" + Module.Name + "_Connect"},
		                       {"disconnectSymbol", Prefix + "_" + Module.Name + "_Disconnect"},
		                       {"releaseObjectSymbol", Prefix + "_" + Module.Name + "_ReleaseObject"}});
	}
	Json Descriptor{{"format", "UMF.ClientABI"},
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
	if (bStructuredErrors)
	{
		Descriptor["errorCodeEncodingVersion"] = Model.ErrorCodeEncodingVersion;
	}
	const std::string ABIHash = Hash::HashBytes(Descriptor.dump());
	Descriptor["nativeABIHash"] = ABIHash;
	Descriptor["contractHash"] = ContractHash;
	Descriptor["projectionHash"] = ProjectionHash;

	std::ostringstream Header, Client;
	Header << NativeCode::Source(Game + ".modabi.h") << "#pragma once\n\n#include <stdint.h>\n";
	if (bStructuredErrors)
	{
		Header << "#include " << NativeCode::Literal(ErrorHeaderName) << "\n";
	}
	Header << "\n#define " << Macro << "_ABI_VERSION 1u\n#define " << Macro << "_ABI_HASH "
	       << NativeCode::Literal(ABIHash) << "\n\n"
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
		Header << "    " << Prefix << "_" << Name << " = "
		       << (bStructuredErrors ? std::string(StatusMacros.at(Name)) : Value.dump()) << ",\n";
	}
	Header << "};\n\n#pragma pack(push, 8)\n";
	Client << NativeCode::Source(Game + ".modclient.cpp") << "#define " << Macro << "_CLIENT_BUILD 1\n#include \""
	       << Game << ".modabi.h\"\n"
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
				       << "_Fn)(" << NativeSignature::Parameters(API, true) << ");\n";
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
		Host << NativeCode::Source(Owner + ".modabi.cpp") << "#define " << Macro << "_HOST_BUILD 1\n#include \"" << Game
		     << ".modabi.h\"\n#include \"ModAPINative.h\"\n#include \"ModAPIHost.h\"\n#include \"" << Game
		     << ".modcalls.h\"\n#include <cstring>\n#include <limits>\n#include <type_traits>\n";
		std::set<std::string> Headers;
		for (const auto& [Name, Type] : Model.Types)
		{
			if (Type.Module == Owner && !Type.HeaderInclude.empty())
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
					if (Parameter.Type.Kind == APIType::EKind::ObjectInput)
					{
						Headers.insert(Parameter.Type.HostInclude);
					}
				}
			}
		}
		for (const auto& Path : Headers)
		{
			Host << "#include " << NativeCode::Literal(Path) << "\n";
		}
		Host << "\nstatic_assert(sizeof(void*) == 8 && sizeof(float) == 4 && sizeof(double) == 8);\n"
		     << "static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559);\n"
		     << "static uint32_t NativeStatus(Mod::API::EError Error)\n{\n    switch (Error)\n    {\n";
		for (const auto& Error : Tools::PublicStatuses::FrameworkStatuses)
		{
			Host << "    case Mod::API::EError::" << Error.Key << ": return " << Prefix << "_" << Error.Key << ";\n";
		}
		Host << "    default: return " << Prefix << "_InternalError;\n    }\n}\n"
		     << "extern \"C\" " << NativeCode::Upper(Owner) << "_API uint32_t UMF_ModHost_" << Owner
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
			       << Macro << "_CLIENT_API uint32_t " << Macro << "_CALL " << Symbol << "("
			       << NativeSignature::Parameters(API, true) << ");\n";
			Host << "extern \"C\" " << Macro << "_HOST_API uint32_t " << Macro << "_CALL " << Symbol << "_Host("
			     << NativeSignature::Parameters(API, true) << ")\n{\n";
			if (API.Return.Kind != APIType::EKind::Void)
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
			Host << "    auto Binding = Mod::API::FNativeAccess::" << (API.Blueprint ? "ResolveObject" : "Resolve")
			     << "(Context, Target, Mod::API::TBindingTraits<" << API.Owner
			     << ">::TypeId(), Mod::API::TBindingTraits<" << API.Owner << ">::Signature);\n"
			     << "    if (!Binding.IsSuccess()) { return NativeStatus(Binding.GetError()); }\n";
			if (API.Blueprint)
			{
				const auto& Type = Model.Types.at(API.Owner);
				Host << "    auto Calls = Binding.GetValue().ResolveCalls(Mod::API::TBindingTraits<" << API.Owner
				     << ">::TypeId(), Mod::API::TBindingTraits<" << API.Owner << ">::Signature);\n"
				     << "    if (!Calls.IsSuccess()) { return NativeStatus(Calls.GetError()); }\n"
				     << "    auto Result = static_cast<const Mod::Generated::" << Type.Symbol
				     << "Calls*>(Calls.GetValue())->" << API.Symbol << "(Binding.GetValue()"
				     << (API.Parameters.empty() ? "" : ", ") << NativeSignature::Arguments(API, true) << ");\n"
				     << "    if (!Result.IsSuccess()) { return NativeStatus(Result.GetError()); }\n";
				if (API.Return.Kind != APIType::EKind::Void)
				{
					Host << "    *OutValue = static_cast<" << NativeSignature::Scalar(API.Return)
					     << ">(Result.GetValue());\n";
				}
				Host << "    return 0;\n#if !PLATFORM_EXCEPTIONS_DISABLED\n    }\n    catch (...) { return " << Prefix
				     << "_InternalError; }\n#endif\n}\n\n";
				continue;
			}
			if (!API.Permissions.empty())
			{
				Host << "    static const FName Permissions[] = {";
				for (const auto& Permission : API.Permissions)
				{
					Host << "FName(TEXT(" << NativeCode::Literal(Permission) << ")),";
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
				if (Type.Kind == APIType::EKind::ObjectInput)
				{
					Host << "    auto Arg" << P
					     << "Object = Mod::API::FHostAccess::ResolveObjectInput(Binding.GetValue(), " << Type.Object
					     << "::StaticClass(), Arg" << P << ", Arg" << P << "Size);\n"
					     << "    if (!Arg" << P << "Object.IsSuccess()) { return NativeStatus(Arg" << P
					     << "Object.GetError()); }\n";
				}
			}
			Host << "    using FMember = " << NativeSignature::Member(API) << ";\n"
			     << "    constexpr FMember Function = static_cast<FMember>(&" << API.DeclaringType << "::" << API.Name
			     << ");\n"
			     << "    " << (API.Return.Kind == APIType::EKind::Void ? "" : "auto Value = ") << "(CastChecked<"
			     << API.Owner << ">(Object.GetValue())->*Function)(" << NativeSignature::Arguments(API, true) << ");\n";
			if (API.Return.Kind == APIType::EKind::Object)
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
			else if (API.Return.Kind != APIType::EKind::Void)
			{
				Host << "    *OutValue = static_cast<" << NativeSignature::Scalar(API.Return) << ">(Value);\n";
			}
			Host << "    return 0;\n#if !PLATFORM_EXCEPTIONS_DISABLED\n    }\n    catch (...) { return " << Prefix
			     << "_InternalError; }\n#endif\n}\n\n";
		}
		Client << "}\n";
		Host << "extern \"C\" " << Macro << "_HOST_API uint32_t " << Macro << "_CALL "
		     << Connection.at("querySymbol").get<std::string>()
		     << "(uint32_t Version, uint32_t Size, const char* ABIHash, " << Table << "* OutAPI)\n{\n"
		     << "    if (!OutAPI || Size != sizeof(" << Table << ")) { return " << Prefix << "_InvalidArgument; }\n"
		     << "    *OutAPI = {};\n    if (Version != 1 || !ABIHash || std::strcmp(ABIHash, "
		     << NativeCode::Literal(ABIHash) << ") != 0) { return " << Prefix << "_ABIMismatch; }\n"
		     << "    OutAPI->StructSize = sizeof(" << Table << ");\n    OutAPI->ABIVersion = 1;\n"
		     << "    std::memcpy(OutAPI->NativeABIHash, " << NativeCode::Literal(ABIHash)
		     << ", 65);\n    OutAPI->ReleaseObject = &NativeRelease;\n";
		Client << "extern \"C\" uint32_t " << Macro << "_CALL " << Connection.at("connectSymbol").get<std::string>()
		       << "(const " << Table << "* Host)\n{\n    if (!Host || Host->StructSize != sizeof(" << Table
		       << ") || Host->ABIVersion != 1 || std::memcmp(Host->NativeABIHash, " << NativeCode::Literal(ABIHash)
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
			       << NativeSignature::Parameters(API, true) << ")\n{\n";
			if (API.Return.Kind != APIType::EKind::Void)
			{
				Client << "    if (!OutValue) { return " << Prefix << "_InvalidArgument; }\n    *OutValue = 0;\n";
			}
			Client << "    auto Function = " << Owner << "_" << API.Symbol << ".load(std::memory_order_acquire);\n"
			       << "    return Function ? Function(" << NativeSignature::Arguments(API, false) << ") : " << Prefix
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
	if (std::any_of(Model.APIs.begin(), Model.APIs.end(), [](const auto& Api) { return Api.Blueprint.has_value(); }))
	{
		Header << "\n#ifdef __cplusplus\n"
		       << "/** @brief Connects each client table using host symbol addresses.\n"
		       << " * @param Resolve Receives an owning module and its query symbol.\n"
		       << " * @return Zero on success, otherwise a native status.\n"
		       << " * @details Call on the game thread before delivering caller tokens.\n"
		       << " */\n"
		       << "template <class FResolve> uint32_t " << Prefix << "_ConnectHost(FResolve Resolve)\n{\n";
		for (const auto& Connection : Connections)
		{
			const std::string Owner = Connection.at("ownerModule");
			const std::string Table = Connection.at("tableType");
			const std::string Query = Connection.at("querySymbol");
			Header << "    " << Table << " " << Owner << "Table{};\n"
			       << "    const auto " << Owner << "Query = reinterpret_cast<decltype(&" << Query << ")>(Resolve("
			       << NativeCode::Literal(Owner) << ", " << NativeCode::Literal(Query) << "));\n"
			       << "    if (!" << Owner << "Query) { return " << Prefix << "_NotConnected; }\n"
			       << "    if (const uint32_t Status = " << Owner << "Query(" << Macro << "_ABI_VERSION, sizeof("
			       << Owner << "Table), " << Macro << "_ABI_HASH, &" << Owner
			       << "Table); Status != 0) { return Status; }\n";
		}
		for (const auto& Connection : Connections)
		{
			Header << "    if (const uint32_t Status = " << Connection.at("connectSymbol").get<std::string>() << "(&"
			       << Connection.at("ownerModule").get<std::string>() << "Table); Status != 0)\n    {\n";
			for (const auto& Other : Connections)
			{
				Header << "        " << Other.at("disconnectSymbol").get<std::string>() << "();\n";
			}
			Header << "        return Status;\n    }\n";
		}
		Header << "    return 0;\n}\n#endif\n";
	}
	Files[HeaderPath] = Header.str();
	Files["Client/" + ClientOwner + "/" + ClientOwner + ".modclient.cpp"] = Client.str();
	Files["Contract/" + Game + ".clientabi.json"] = Descriptor.dump(2) + "\n";

	Json SdkAPIs = Json::array(), SdkTypes = Json::array(), Unsupported = Json::array();
	std::set<std::string> DirectResults;
	std::map<std::string, const API*> Names;
	for (const auto& API : Model.APIs)
	{
		if (API.Return.Kind == APIType::EKind::Object && !API.Return.bConst && API.Parameters.empty() &&
		    API.Scope == "Player" && Model.Types.at(API.Owner).bLocalPlayerSubsystem)
		{
			DirectResults.insert(API.Return.Object);
		}
	}
	std::set<std::string> ViewTypes = DirectResults;
	for (const auto& [Name, Type] : Model.Types)
	{
		if (Type.bFullView || !Type.BlueprintClass.empty())
		{
			ViewTypes.insert(Name);
		}
	}
	for (std::size_t Previous = 0; Previous != ViewTypes.size();)
	{
		Previous = ViewTypes.size();
		for (const auto& API : Model.APIs)
		{
			if (API.Return.Kind == APIType::EKind::Object &&
			    (ViewTypes.contains(API.Owner) || Model.Types.at(API.Owner).bLocalPlayerSubsystem))
			{
				ViewTypes.insert(API.Return.Object);
			}
		}
	}
	for (const auto& Name : ViewTypes)
	{
		const auto& Type = Model.Types.at(Name);
		Json Entry{{"id", Type.Identity.Id}, {"kind", "ObjectReference"}, {"cppName", "FModObject_" + Type.Symbol}};
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
		const bool Getter = API.Return.Kind == APIType::EKind::Object && !API.Return.bConst && API.Parameters.empty() &&
		                    API.Scope == "Player" && Model.Types.at(API.Owner).bLocalPlayerSubsystem;
		const bool Method = API.Return.Public == "float" && API.Parameters.size() == 1 &&
		                    API.Parameters[0].Type.Public == "float" && DirectResults.contains(API.Owner) &&
		                    !API.Blueprint;
		const bool bPlayerProvider = API.Scope == "Player" && Model.Types.at(API.Owner).bLocalPlayerSubsystem;
		const bool bCanRepresentInputs =
		    std::all_of(API.Parameters.begin(), API.Parameters.end(),
		                [](const auto& Parameter)
		                {
			                const auto& Type = Parameter.Type;
			                return Type.Kind == APIType::EKind::Scalar ||
			                       (Type.Kind == APIType::EKind::ObjectInput && !Type.EngineInclude.empty() &&
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
		Json Entry{{"id", API.Id},
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
		if (API.SourceErrorCodes)
		{
			Entry["sourceErrorCodes"] = *API.SourceErrorCodes;
		}
		if (API.Blueprint)
		{
			Entry["hostBlueprint"] = {{"callingTableHash", Model.Types.at(API.Owner).Signature}};
		}
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
			// Names and IDs are derived once by MHT; MDT does not infer them.
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
			Json Parameters = Json::array();
			bool bBlueprint =
			    API.Return.Kind != APIType::EKind::Scalar || NativeSignature::IsBlueprintScalar(API.Return);
			for (const auto& Parameter : API.Parameters)
			{
				const auto& Type = Parameter.Type;
				if (Type.Kind == APIType::EKind::ObjectInput)
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
					bBlueprint &= NativeSignature::IsBlueprintScalar(Type);
				}
			}
			Entry["parameters"] = std::move(Parameters);
			Entry["blueprint"] = bBlueprint;
			if (API.Return.Kind == APIType::EKind::Object)
			{
				Entry["result"] = {{"kind", "ObjectReference"},
				                   {"typeId", Model.Types.at(API.Return.Object).Identity.Id}};
			}
			else if (API.Return.Kind == APIType::EKind::Scalar)
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
	Json Sdk{{"format", "UMF.SdkContract"},
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
	if (bStructuredErrors)
	{
		Sdk["errorCodeEncodingVersion"] = Model.ErrorCodeEncodingVersion;
		Sdk["errorHeader"] = ErrorHeader;
	}
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
