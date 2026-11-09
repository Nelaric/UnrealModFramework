// Copyright (c) 2026 Nelaric Contributors
#include "NativeAbiGenerator.h"

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

std::string Scalar(const FApiType& Type)
{
	if (Type.Kind == EApiTypeKind::Object)
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

std::string Parameters(const FApi& Api, bool bNames)
{
	std::string Result = "uint64_t" + std::string(bNames ? " Context" : "") + ", uint64_t" + (bNames ? " Target" : "");
	for (std::size_t I = 0; I < Api.Parameters.size(); ++I)
	{
		Result += ", " + Scalar(Api.Parameters[I].Type) + (bNames ? " Arg" + std::to_string(I) : "");
	}
	if (Api.Return.Kind != EApiTypeKind::Void)
	{
		Result += ", " + Scalar(Api.Return) + "*" + (bNames ? " OutValue" : "");
	}
	return Result;
}

std::string Arguments(const FApi& Api, bool bSource)
{
	std::string Result = bSource ? "" : "Context, Target";
	for (std::size_t I = 0; I < Api.Parameters.size(); ++I)
	{
		if (!Result.empty())
		{
			Result += ", ";
		}
		const auto Arg = "Arg" + std::to_string(I);
		Result += bSource ? "static_cast<" + Api.Parameters[I].Type.Cpp + ">(" + Arg + ")" : Arg;
	}
	return Result + (!bSource && Api.Return.Kind != EApiTypeKind::Void ? ", OutValue" : "");
}

std::string Member(const FApi& Api)
{
	std::string Result = Api.Return.Cpp + " (" + Api.Owner + "::*)(";
	for (std::size_t I = 0; I < Api.Parameters.size(); ++I)
	{
		Result += (I ? ", " : "") + Api.Parameters[I].Type.Cpp;
	}
	return Result + ")" + (Api.bConst ? " const" : "") + (Api.bNoexcept ? " noexcept" : "");
}

const std::vector<std::string> Errors = {
    "None",          "WrongThread",      "BindingExpired", "NotRegistered",  "SignatureMismatch", "ObjectUnavailable",
    "ScopeMismatch", "PermissionDenied", "ReadOnly",       "InvalidArgument"};
} // namespace

FJson WriteNativeAbi(const FGenerationModel& Model, std::map<std::string, std::string>& Files,
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
	FJson Apis = FJson::array(), Required = FJson::array(), Connections = FJson::array();
	for (const FApi& Api : Model.Apis)
	{
		const std::string Symbol = Prefix + "_" + Api.Module + "_" + Api.Symbol;
		FJson Params = FJson::array({"uint64_t", "uint64_t"});
		for (const auto& Param : Api.Parameters)
		{
			Params.push_back(Scalar(Param.Type));
		}
		if (Api.Return.Kind != EApiTypeKind::Void)
		{
			Params.push_back(Scalar(Api.Return) + "*");
		}
		const FJson Signature{{"callingConvention", "cdecl"}, {"returns", "uint32_t"}, {"parameters", Params}};
		const std::string SignatureHash = HashBytes(Signature.dump());
		const std::string ApiProjection = HashBytes(Api.SourceSignature + Signature.dump() + ":ContextHandlesV1");
		Apis.push_back({{"id", Api.Id},
		                {"version", Api.Version},
		                {"ownerModule", Api.Module},
		                {"symbol", Symbol},
		                {"hostSymbol", Symbol + "_Host"},
		                {"slot", Api.Symbol},
		                {"signature", Signature},
		                {"signatureHash", SignatureHash},
		                {"projectionHash", ApiProjection}});
		for (const auto& Owner : {Api.Module, ClientOwner})
		{
			Required.push_back({{"ownerModule", Owner},
			                    {"symbol", Symbol + (Owner == Api.Module ? "_Host" : "")},
			                    {"exportId", Api.Id},
			                    {"signatureHash", SignatureHash},
			                    {"projectionHash", ApiProjection}});
		}
	}
	FJson StatusCodes = FJson::object();
	for (std::size_t I = 0; I < Errors.size(); ++I)
	{
		StatusCodes[Errors[I]] = I;
	}
	StatusCodes["NotConnected"] = 65536;
	StatusCodes["AbiMismatch"] = 65537;
	StatusCodes["InternalError"] = 65538;
	for (const auto& Module : Model.Input.Modules)
	{
		Connections.push_back({{"ownerModule", Module.Name},
		                       {"tableType", Prefix + "_" + Module.Name + "_FHostApiV1"},
		                       {"querySymbol", Prefix + "_" + Module.Name + "_GetHostApiV1"},
		                       {"connectSymbol", Prefix + "_" + Module.Name + "_Connect"},
		                       {"disconnectSymbol", Prefix + "_" + Module.Name + "_Disconnect"},
		                       {"releaseObjectSymbol", Prefix + "_" + Module.Name + "_ReleaseObject"}});
	}
	FJson Descriptor{{"format", "UMF.ClientAbi"},
	                 {"schemaVersion", 1},
	                 {"status", "Complete"},
	                 {"adapterAbi", "ContextHandlesV1"},
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
	                 {"apis", Apis}};
	Descriptor["contextDelivery"] = {{"issuer", "Mod::Api::FNativeAccess::Open"},
	                                 {"scopeToken", "HostIssuedCallerScope"},
	                                 {"objectToken", "ContextOwnedNonOwning"},
	                                 {"revocation", "CallerScopeOrRegistryRevocation"},
	                                 {"releaseRule", "ReleaseInvalidatesAllCopiesOfTheSameToken"},
	                                 {"unloadRule", "DisconnectAndQuiesceBeforeHostModuleUnload"}};
	const std::string AbiHash = HashBytes(Descriptor.dump());
	Descriptor["nativeAbiHash"] = AbiHash;
	Descriptor["contractHash"] = ContractHash;
	Descriptor["projectionHash"] = ProjectionHash;

	std::ostringstream Header, Client;
	Header << Source(Game + ".modabi.h") << "#pragma once\n\n#include <stdint.h>\n\n"
	       << "#define " << Macro << "_ABI_VERSION 1u\n#define " << Macro << "_ABI_HASH " << Literal(AbiHash) << "\n\n"
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
		for (std::size_t I = 0; I < Model.Apis.size(); ++I)
		{
			const auto& Api = Model.Apis[I];
			if (Api.Module == Owner)
			{
				Header << "typedef uint32_t (" << Macro << "_CALL *" << Apis[I].at("symbol").get<std::string>()
				       << "_Fn)(" << Parameters(Api, true) << ");\n";
			}
		}
		Header << "typedef struct " << Table << "\n{\n    uint32_t StructSize;\n    uint32_t AbiVersion;\n"
		       << "    char NativeAbiHash[65];\n    " << ReleaseType << " ReleaseObject;\n";
		for (std::size_t I = 0; I < Model.Apis.size(); ++I)
		{
			if (Model.Apis[I].Module == Owner)
			{
				Header << "    " << Apis[I].at("symbol").get<std::string>() << "_Fn " << Model.Apis[I].Symbol << ";\n";
			}
		}
		Header << "} " << Table << ";\n\n"
		       << "/** @brief Queries a matching host table before connecting the client. */\n"
		       << Macro << "_HOST_API uint32_t " << Macro << "_CALL " << Connection.at("querySymbol").get<std::string>()
		       << "(uint32_t Version, uint32_t Size, const char* AbiHash, " << Table << "* OutApi);\n"
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
		     << ".modabi.h\"\n#include \"ModApiNative.h\"\n#include \"ModApiHost.h\"\n#include \"" << Game
		     << ".modcalls.h\"\n#include <cstring>\n#include <limits>\n#include <type_traits>\n";
		std::set<fs::path> Headers;
		const auto& Module = *std::find_if(Model.Input.Modules.begin(), Model.Input.Modules.end(),
		                                   [&](const auto& Value) { return Value.Name == Owner; });
		for (const auto& [Name, Type] : Model.Types)
		{
			if (Type.Module == Owner)
			{
				Headers.insert(Type.Header);
			}
		}
		for (const auto& Path : Headers)
		{
			Host << "#include " << Literal(PathToUtf8(Path.lexically_relative(Module.Root))) << "\n";
		}
		Host << "\nstatic_assert(sizeof(void*) == 8 && sizeof(float) == 4 && sizeof(double) == 8);\n"
		     << "static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559);\n"
		     << "static uint32_t NativeStatus(Mod::Api::EError Error)\n{\n    switch (Error)\n    {\n";
		for (const auto& Error : Errors)
		{
			Host << "    case Mod::Api::EError::" << Error << ": return " << Prefix << "_" << Error << ";\n";
		}
		Host << "    default: return " << Prefix << "_InternalError;\n    }\n}\n"
		     << "extern \"C\" " << Upper(Owner) << "_API uint32_t UMF_ModHost_" << Owner
		     << "_RetainAbi(void) { return 1; }\n"
		     << "static uint32_t " << Macro << "_CALL NativeRelease(uint64_t Context, uint64_t Object)\n"
		     << "{ return NativeStatus(Mod::Api::FNativeAccess::Release(Context, Object).GetError()); }\n\n";
		Client << "namespace\n{\nstd::atomic<" << ReleaseType << "> " << Owner << "_Release{nullptr};\n";
		for (std::size_t I = 0; I < Model.Apis.size(); ++I)
		{
			const auto& Api = Model.Apis[I];
			if (Api.Module != Owner)
			{
				continue;
			}
			const std::string Symbol = Apis[I].at("symbol");
			Client << "std::atomic<" << Symbol << "_Fn> " << Owner << "_" << Api.Symbol << "{nullptr};\n";
			Header << "/** @brief Calls " << Api.Id << "; output is zero on failure. */\n"
			       << Macro << "_CLIENT_API uint32_t " << Macro << "_CALL " << Symbol << "(" << Parameters(Api, true)
			       << ");\n";
			Host << "extern \"C\" " << Macro << "_HOST_API uint32_t " << Macro << "_CALL " << Symbol << "_Host("
			     << Parameters(Api, true) << ")\n{\n";
			if (Api.Return.Kind != EApiTypeKind::Void)
			{
				Host << "    if (!OutValue) { return " << Prefix << "_InvalidArgument; }\n    *OutValue = 0;\n";
			}
			Host << "#if !PLATFORM_EXCEPTIONS_DISABLED\n    try\n    {\n#endif\n";
			for (std::size_t P = 0; P < Api.Parameters.size(); ++P)
			{
				if (Api.Parameters[P].Type.Public == "bool")
				{
					Host << "    if (Arg" << P << " > 1) { return " << Prefix << "_InvalidArgument; }\n";
				}
			}
			Host << "    auto Binding = Mod::Api::FNativeAccess::Resolve(Context, Target, Mod::Api::TBindingTraits<"
			     << Api.Owner << ">::TypeId(), Mod::Api::TBindingTraits<" << Api.Owner << ">::Signature);\n"
			     << "    if (!Binding.IsSuccess()) { return NativeStatus(Binding.GetError()); }\n";
			if (!Api.Permissions.empty())
			{
				Host << "    static const FName Permissions[] = {";
				for (const auto& Permission : Api.Permissions)
				{
					Host << "FName(TEXT(" << Literal(Permission) << ")),";
				}
				Host << "};\n";
			}
			Host << "    auto Object = Mod::Api::FHostAccess::Enter(Binding.GetValue(), Mod::Api::TBindingTraits<"
			     << Api.Owner << ">::TypeId(), Mod::Api::EScope::" << Api.Scope << ", "
			     << (Api.Permissions.empty() ? "TConstArrayView<FName>()" : "MakeArrayView(Permissions)") << ", "
			     << (Api.bConst ? "false" : "true") << ");\n"
			     << "    if (!Object.IsSuccess()) { return NativeStatus(Object.GetError()); }\n"
			     << "    using FMember = " << Member(Api) << ";\n"
			     << "    constexpr FMember Function = static_cast<FMember>(&" << Api.Owner << "::" << Api.Name << ");\n"
			     << "    " << (Api.Return.Kind == EApiTypeKind::Void ? "" : "auto Value = ") << "(CastChecked<"
			     << Api.Owner << ">(Object.GetValue())->*Function)(" << Arguments(Api, true) << ");\n";
			if (Api.Return.Kind == EApiTypeKind::Object)
			{
				Host << "    auto Projected = Mod::Api::FHostAccess::ProjectObject(Binding.GetValue(), const_cast<"
				     << Api.Return.Object << "*>(Value), Mod::Api::TBindingTraits<" << Api.Return.Object
				     << ">::TypeId(), Mod::Api::TBindingTraits<" << Api.Return.Object << ">::Signature, "
				     << (Api.Return.bConst ? "true" : "false") << ");\n"
				     << "    if (!Projected.IsSuccess()) { return NativeStatus(Projected.GetError()); }\n"
				     << "    auto Token = Mod::Api::FNativeAccess::Publish(Context, MoveTemp(Projected.GetValue()));\n"
				     << "    if (!Token.IsSuccess()) { return NativeStatus(Token.GetError()); }\n    *OutValue = "
				        "Token.GetValue();\n";
			}
			else if (Api.Return.Kind != EApiTypeKind::Void)
			{
				Host << "    *OutValue = static_cast<" << Scalar(Api.Return) << ">(Value);\n";
			}
			Host << "    return 0;\n#if !PLATFORM_EXCEPTIONS_DISABLED\n    }\n    catch (...) { return " << Prefix
			     << "_InternalError; }\n#endif\n}\n\n";
		}
		Client << "}\n";
		Host << "extern \"C\" " << Macro << "_HOST_API uint32_t " << Macro << "_CALL "
		     << Connection.at("querySymbol").get<std::string>()
		     << "(uint32_t Version, uint32_t Size, const char* AbiHash, " << Table << "* OutApi)\n{\n"
		     << "    if (!OutApi || Size != sizeof(" << Table << ")) { return " << Prefix << "_InvalidArgument; }\n"
		     << "    *OutApi = {};\n    if (Version != 1 || !AbiHash || std::strcmp(AbiHash, " << Literal(AbiHash)
		     << ") != 0) { return " << Prefix << "_AbiMismatch; }\n"
		     << "    OutApi->StructSize = sizeof(" << Table << ");\n    OutApi->AbiVersion = 1;\n"
		     << "    std::memcpy(OutApi->NativeAbiHash, " << Literal(AbiHash)
		     << ", 65);\n    OutApi->ReleaseObject = &NativeRelease;\n";
		Client << "extern \"C\" uint32_t " << Macro << "_CALL " << Connection.at("connectSymbol").get<std::string>()
		       << "(const " << Table << "* Host)\n{\n    if (!Host || Host->StructSize != sizeof(" << Table
		       << ") || Host->AbiVersion != 1 || std::memcmp(Host->NativeAbiHash, " << Literal(AbiHash)
		       << ", 65) != 0 || !Host->ReleaseObject";
		for (std::size_t I = 0; I < Model.Apis.size(); ++I)
		{
			if (Model.Apis[I].Module == Owner)
			{
				Host << "    OutApi->" << Model.Apis[I].Symbol << " = &" << Apis[I].at("symbol").get<std::string>()
				     << "_Host;\n";
				Client << " || !Host->" << Model.Apis[I].Symbol;
			}
		}
		Host << "    return 0;\n}\n";
		Client << ") { return " << Prefix << "_AbiMismatch; }\n";
		for (const auto& Api : Model.Apis)
		{
			if (Api.Module == Owner)
			{
				Client << "    " << Owner << "_" << Api.Symbol << ".store(Host->" << Api.Symbol
				       << ", std::memory_order_release);\n";
			}
		}
		Client << "    " << Owner
		       << "_Release.store(Host->ReleaseObject, std::memory_order_release);\n    return 0;\n}\n"
		       << "extern \"C\" uint32_t " << Macro << "_CALL " << Connection.at("disconnectSymbol").get<std::string>()
		       << "(void)\n{\n";
		for (const auto& Api : Model.Apis)
		{
			if (Api.Module == Owner)
			{
				Client << "    " << Owner << "_" << Api.Symbol << ".store(nullptr, std::memory_order_release);\n";
			}
		}
		Client << "    " << Owner << "_Release.store(nullptr, std::memory_order_release);\n    return 0;\n}\n"
		       << "extern \"C\" uint32_t " << Macro << "_CALL "
		       << Connection.at("releaseObjectSymbol").get<std::string>()
		       << "(uint64_t Context, uint64_t Object)\n{\n    auto Function = " << Owner
		       << "_Release.load(std::memory_order_acquire);\n"
		       << "    return Function ? Function(Context, Object) : " << Prefix << "_NotConnected;\n}\n";
		for (std::size_t I = 0; I < Model.Apis.size(); ++I)
		{
			const auto& Api = Model.Apis[I];
			if (Api.Module != Owner)
			{
				continue;
			}
			Client << "extern \"C\" uint32_t " << Macro << "_CALL " << Apis[I].at("symbol").get<std::string>() << "("
			       << Parameters(Api, true) << ")\n{\n";
			if (Api.Return.Kind != EApiTypeKind::Void)
			{
				Client << "    if (!OutValue) { return " << Prefix << "_InvalidArgument; }\n    *OutValue = 0;\n";
			}
			Client << "    auto Function = " << Owner << "_" << Api.Symbol << ".load(std::memory_order_acquire);\n"
			       << "    return Function ? Function(" << Arguments(Api, false) << ") : " << Prefix
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

	FJson SdkApis = FJson::array(), SdkTypes = FJson::array(), Unsupported = FJson::array();
	std::set<std::string> Returned, Names;
	for (const auto& Api : Model.Apis)
	{
		if (Api.Return.Kind == EApiTypeKind::Object && !Api.Return.bConst && Api.Parameters.empty() &&
		    Api.Scope == "Player" && Model.Types.at(Api.Owner).bLocalPlayerSubsystem)
		{
			Returned.insert(Api.Return.Object);
		}
	}
	for (const auto& Name : Returned)
	{
		const auto& Type = Model.Types.at(Name);
		SdkTypes.push_back(
		    {{"id", Type.Identity.Id}, {"kind", "ObjectReference"}, {"cppName", "FModObject_" + Type.Symbol}});
	}
	bool bGetter = false, bMethod = false;
	for (std::size_t I = 0; I < Model.Apis.size(); ++I)
	{
		const auto& Api = Model.Apis[I];
		const bool Getter = Api.Return.Kind == EApiTypeKind::Object && !Api.Return.bConst && Api.Parameters.empty() &&
		                    Api.Scope == "Player" && Model.Types.at(Api.Owner).bLocalPlayerSubsystem;
		const bool Method = Api.Return.Public == "float" && Api.Parameters.size() == 1 &&
		                    Api.Parameters[0].Type.Public == "float" && Returned.contains(Api.Owner);
		if (!Getter && !Method)
		{
			Unsupported.push_back(Api.Id);
			continue;
		}
		std::string Name = Api.Name;
		std::string Fold = Name;
		std::transform(Fold.begin(), Fold.end(), Fold.begin(), [](unsigned char C) { return std::tolower(C); });
		if (!Names.insert(Fold).second)
		{
			Name += "_" + Api.Symbol;
		}
		FJson Entry{{"id", Api.Id},
		            {"name", Name},
		            {"displayName", Api.Name},
		            {"shape", Getter ? "ObjectGetter" : "FloatMethod"},
		            {"scope", Api.Scope},
		            {"thread", "GameThread"},
		            {"failureRule", "ReturnError"},
		            {"permissions", Api.Permissions},
		            {"signatureHash", Apis[I].at("signatureHash")},
		            {"projectionHash", Apis[I].at("projectionHash")},
		            {"native", {{"header", HeaderPath}, {"symbol", Apis[I].at("symbol")}}}};
		if (Getter)
		{
			bGetter = true;
			Entry["resultTypeId"] = Model.Types.at(Api.Return.Object).Identity.Id;
			Entry["provider"] = "LocalPlayerSubsystem";
			Entry["nullResult"] = "Error";
		}
		else
		{
			bMethod = true;
			Entry["targetTypeId"] = Model.Types.at(Api.Owner).Identity.Id;
			// Names and IDs are derived once by MHT; MST does not infer them.
			Entry["parameterName"] = "Arg0";
			Entry["parameterId"] = Api.Id + ":parameter:0";
		}
		SdkApis.push_back(std::move(Entry));
	}
	FJson Sdk{{"format", "UMF.SdkContract"},
	          {"schemaVersion", 1},
	          {"adapterAbi", "ContextHandlesV1"},
	          {"status", Unsupported.empty() && bGetter && bMethod ? "Complete" : "Unsupported"},
	          {"unresolvedTypeCount", 0},
	          {"gameId", Game},
	          {"contractHash", ContractHash},
	          {"projectionHash", ProjectionHash},
	          {"nativeAbiHash", AbiHash},
	          {"types", SdkTypes},
	          {"apis", SdkApis},
	          {"unsupportedApis", Unsupported},
	          {"connectionDescriptor", "Contract/" + Game + ".clientabi.json"}};
	if (!Lifecycle.is_null())
	{
		Sdk["modLifecycle"] = Lifecycle;
	}
	Files["Contract/" + Game + ".sdkcontract.json"] = Sdk.dump(2) + "\n";
	return {{"nativeAbiHash", AbiHash},
	        {"requiredSymbols", Required},
	        {"clientOwnerModule", ClientOwner},
	        {"descriptor", "Contract/" + Game + ".clientabi.json"},
	        {"sdkContract", "Contract/" + Game + ".sdkcontract.json"}};
}
} // namespace Mod::HeaderTool
