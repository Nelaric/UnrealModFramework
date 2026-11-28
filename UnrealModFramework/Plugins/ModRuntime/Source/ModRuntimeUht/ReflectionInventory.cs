// Copyright (c) 2026 Nelaric Contributors
using System;
using System.Collections.Generic;
using System.Linq;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace Mod.Uht;

/// <summary>Records this UHT session's reflected identities without copying native layout.</summary>
internal static class ReflectionInventory
{
	private sealed record Entry(string cpp, string module, string path, string file, string legacyId);

	internal static object Read(IUhtExportFactory factory) =>
		factory
			.Session.Modules.SelectMany(module =>
				module.Headers.SelectMany(header => Collect(header.Children, module.Module.Name, header.FilePath))
			)
			.OrderBy(type => type.path, StringComparer.Ordinal)
			.ToArray();

	private static IEnumerable<Entry> Collect(IEnumerable<UhtType> children, string module, string file)
	{
		foreach (UhtType type in children)
		{
			if (type is UhtClass or UhtScriptStruct or UhtEnum)
			{
				yield return new Entry(
					type.SourceName,
					module,
					"/Script/" + module + "." + type.EngineName,
					file,
					type.MetaData.GetValueOrDefault("ModTypeId")
				);
			}
			foreach (Entry nested in Collect(type.Children, module, file))
			{
				yield return nested;
			}
		}
	}
}
