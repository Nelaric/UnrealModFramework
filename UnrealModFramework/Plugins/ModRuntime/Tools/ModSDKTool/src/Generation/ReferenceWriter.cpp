// Copyright (c) 2026 Nelaric Contributors
#include "ReferenceWriter.h"

#include "ModSDKTool/src/Generation/CppLiteral.h"

#include <sstream>
#include <string>

namespace Mod::SdkTool
{
void ReferenceWriter::WriteReference(std::ostringstream& Header, const std::string& Name, const std::string& SecondName,
                                     const std::string& APIMacro)
{
	Header << CppLiteral::LineDoc << " Opaque values supplied by the host; no game object layout is exposed.\n"
	       << "USTRUCT(BlueprintType)\nstruct " << APIMacro << " " << Name << "\n{\n\tGENERATED_BODY()\n\npublic:\n"
	       << "\t" << CppLiteral::LineDoc << " Creates a reference from host-owned, process-local tokens.\n"
	       << "\tstatic " << Name << " FromNative(uint64 InContext, uint64 In" << SecondName << ")\n\t{\n"
	       << "\t\t" << Name << " Result;\n\t\tResult.Context = InContext;\n\t\tResult." << SecondName << " = In"
	       << SecondName << ";\n\t\treturn Result;\n\t}\n\n"
	       << "\t" << CppLiteral::LineDoc << " Returns the host-issued calling context token.\n"
	       << "\tuint64 GetNativeContext() const { return Context; }\n\n"
	       << "\t" << CppLiteral::LineDoc << " Returns the host-issued target token.\n"
	       << "\tuint64 GetNative" << SecondName << "() const { return " << SecondName << "; }\n\n"
	       << "private:\n\tUPROPERTY(Transient)\n\tuint64 Context = 0;\n\n"
	       << "\tUPROPERTY(Transient)\n\tuint64 " << SecondName << " = 0;\n};\n\n";
}
} // namespace Mod::SdkTool
