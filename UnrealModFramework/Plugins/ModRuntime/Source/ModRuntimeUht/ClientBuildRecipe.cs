// Copyright (c) 2026 Nelaric Contributors
using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;
using EpicGames.UHT.Utils;

namespace Mod.Uht;

/// <summary>Derives a standalone C ABI recipe from the selected real toolchain.</summary>
internal static class ClientBuildRecipe
{
	internal static string Prepare(
		string hostContext,
		string game,
		string owner,
		string generated,
		string outputDirectory
	)
	{
		JsonObject context = JsonNode.Parse(File.ReadAllText(hostContext))!.AsObject();
		JsonObject compiler = context["compiler"]!.AsObject();
		List<string> original = new();
		foreach (JsonNode? value in compiler["arguments"]!.AsArray())
		{
			original.Add(value!.GetValue<string>());
		}
		JsonArray arguments = new();
		for (int index = 0; index < original.Count; ++index)
		{
			string argument = original[index];
			if (
				argument.StartsWith("/FI", StringComparison.OrdinalIgnoreCase)
				|| argument.StartsWith("/I", StringComparison.OrdinalIgnoreCase)
			)
			{
				if (argument.Length == (argument.StartsWith("/FI", StringComparison.OrdinalIgnoreCase) ? 3 : 2))
				{
					++index;
				}
				continue;
			}
			if (argument.StartsWith("/external:I", StringComparison.OrdinalIgnoreCase))
			{
				string include = argument.Length == 11 ? original[++index] : argument[11..];
				// UE's relative external roots are unnecessary for a pure standard-C++ client.
				if (Path.IsPathFullyQualified(include))
				{
					arguments.Add("/external:I" + include);
				}
				continue;
			}
			arguments.Add(argument);
		}
		arguments.Add("/I" + Path.Combine(generated, "SDK", "Public"));
		arguments.Add("/D" + owner.ToUpperInvariant() + "_API=");
		compiler["arguments"] = arguments;
		string library = Path.Combine(outputDirectory, owner + ".lib");
		string path = library + ".context.json";
		context["ownerModule"] = owner;
		context["registrationAnchor"] = "UMF_ModClient_" + game;
		context["workingDirectory"] = outputDirectory;
		context["outputLibrary"] = library;
		context["environment"]!["TEMP"] = outputDirectory;
		context["environment"]!["TMP"] = outputDirectory;
		context.Remove("owningBinary");
		context.Remove("linkVerification");
		JsonArray dependencies = new();
		foreach (JsonNode? value in context["dependencies"]!.AsArray())
		{
			string dependency = value!.GetValue<string>();
			if (!dependency.EndsWith(".h", StringComparison.OrdinalIgnoreCase))
			{
				dependencies.Add(dependency);
			}
		}
		context["dependencies"] = dependencies;
		context["clientABI"] = "ContextHandlesV1";
		Directory.CreateDirectory(Path.GetDirectoryName(path)!);
		string document = context.ToJsonString(
			new JsonSerializerOptions(JsonSerializerOptions.Default) { WriteIndented = true }
		);
		if (!File.Exists(path) || File.ReadAllText(path) != document)
		{
			File.WriteAllText(path, document);
		}
		if (original.Count == 0)
		{
			throw new UhtException("ModRuntime MBT: the selected client toolchain recipe is empty.");
		}
		return path;
	}
}
