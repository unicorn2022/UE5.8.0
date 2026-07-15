// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using AutomationUtils.Automation;
using EpicGames.Core;

namespace AutomationUtils;

public class CloudAppDependencyTree
{
	
	/** A single node in the dependency tree representing a content artifact. */
	private class Entry
	{
		/** The product namespace that owns this artifact. */
		public string Namespace = "";
		/** The catalog ID used to entitle this artifact. */
		public string CatalogItemId = "";
		/** The artifact IDs that this entry depends on. */
		public HashSet<string> Dependencies = new();

		public JsonObject ToJson()
		{
			JsonObject Json = new JsonObject();
			Json.AddOrSetFieldValue("namespace", Namespace);
			Json.AddOrSetFieldValue("catalogItemId", CatalogItemId);
			Json.AddOrSetFieldValue("dependencies", Dependencies.ToArray());
			return Json;
		}

		public void FromJson(JsonObject Json)
		{
			Namespace = Json.GetStringField("namespace");
			CatalogItemId = Json.GetStringField("catalogItemId");
			Dependencies = Json.GetStringArrayField("dependencies").ToHashSet();
		}
	}
	
	/** Alias -> Artifact ID */
	private Dictionary<string, string> Aliases;
	/** Artifact ID -> Entry */
	private Dictionary<string, Entry> Entries;
	
	public CloudAppDependencyTree()
	{
		Aliases = new Dictionary<string, string>();
		Entries = new Dictionary<string, Entry>();
	}

	public IEnumerable<string> GetAliases()
	{
		return Aliases.Keys;
	}

	public void AddEntry(string CloudAppName, string ArtifactId, string Namespace, string CatalogItemId, List<string> Dependencies)
	{
		if (string.IsNullOrEmpty(CloudAppName))
		{
			throw new ArgumentException("Cannot add an entry with an empty or null cloud app name to a dependency tree.");
		}
		if (string.IsNullOrEmpty(ArtifactId))
		{
			throw new ArgumentException("Cannot add an entry with an empty or null artifact ID to a dependency tree.");
		}
		if (string.IsNullOrEmpty(Namespace))
		{
			throw new ArgumentException("Cannot add an entry with an empty or null namespace to a dependency tree.");
		}
		if (string.IsNullOrEmpty(CatalogItemId))
		{
			throw new ArgumentException("Cannot add an entry with an empty or null catalog ID to a dependency tree.");
		}
		Entry NewEntry = new Entry
		{
			Namespace = Namespace,
			CatalogItemId = CatalogItemId,
			Dependencies = Dependencies?.ToHashSet() ?? new HashSet<string>()
		};
		Entries.Add(ArtifactId, NewEntry);
		Aliases.Add(CloudAppName, ArtifactId);
	}

	/// <summary>
	/// Updates the ID values associated the provided cloud app.
	/// </summary>
	/// <param name="CloudAppName">The name/alias of the cloud app to update.</param>
	/// <param name="NewArtifactId">The new artifact ID to associate with the cloud app.</param>
	/// <param name="NewNamespace">The new product namespace to associate with the cloud app.</param>
	/// <param name="NewCatalogId">The new catalog item ID to associate with the cloud app.</param>
	/// <exception cref="ArgumentException">If any provided arguments are null or empty, or if the specified cloud app does not exist in this tree.</exception>
	public void UpdateCloudAppIdValues(string CloudAppName, string NewArtifactId, string NewNamespace, string NewCatalogId)
	{
		if (!Aliases.TryGetValue(CloudAppName, out string OldArtifactId))
		{
			throw new ArgumentException($"Cannot update cloud app '{CloudAppName}' because it does not exist in this tree.");
		}
		if (Entries.ContainsKey(NewArtifactId))
		{
			throw new ArgumentException($"Cannot update cloud app '{CloudAppName}' with the new artifact ID '{NewArtifactId}' because the new value is already present in the tree.");
		}
		if (NewCatalogId == null)
		{
			throw new ArgumentException("Null catalog item ID values are not valid for use in cloud app dependency trees.");
		}
		if (string.IsNullOrEmpty(NewNamespace))
		{
			throw new ArgumentException("Empty or null namespace values are not valid for use in cloud app dependency trees.");
		}
		// Swap entry key
		if (Entries.Remove(OldArtifactId, out Entry OldEntry))
		{
			OldEntry.CatalogItemId = NewCatalogId;
			OldEntry.Namespace = NewNamespace;
			Entries.Add(NewArtifactId, OldEntry);
		}
		// Swap alias table entries
		List<string> AliasKeys = Aliases.Keys.ToList();
		foreach (var Alias in AliasKeys)
		{
			if (Aliases.TryGetValue(Alias, out string Id) && Id == OldArtifactId)
			{
				Aliases[Alias] = NewArtifactId;
			}
		}
		// Swap dependencies
		foreach (KeyValuePair<string, Entry> Node in Entries)
		{
			if (Node.Value.Dependencies.Remove(OldArtifactId))
			{
				Node.Value.Dependencies.Add(NewArtifactId);
			}
		}
	}

	private static string SanitizeCloudAppName(string CloudAppName, string BaseAppName)
	{
		return string.IsNullOrWhiteSpace(CloudAppName) ? BaseAppName : CloudAppName;
	}

	/// <summary>
	/// Creates a dependency tree containing placeholder values for the artifact ID, catalog ID, and namespace on each entry. For production use please
	/// update the values of the generated tree for each cloud app by using the UpdateCloudAppIdValues function.
	/// </summary>
	/// <param name="BaseAppName">The cloud app alias for all base game content.</param>
	/// <param name="Bundles">The bundles to use to build the dependency tree.</param>
	/// <returns></returns>
	public static CloudAppDependencyTree CreateTemplateFromBundleConfig(string BaseAppName, IReadOnlyDictionary<string, BundleUtils.BundleSettings> Bundles)
	{
		CloudAppDependencyTree TemplateTree = new CloudAppDependencyTree();
		Dictionary<string, HashSet<string>> CloudAppsToDependencies = new Dictionary<string, HashSet<string>>();
		CloudAppsToDependencies.Add(BaseAppName, new HashSet<string>());
		foreach (BundleUtils.BundleSettings Bundle in Bundles.Values)
		{
			if (string.IsNullOrWhiteSpace(Bundle.CloudAppNameOverride))
			{
				// This bundle does not participate in the modular cloud app system, skip since it will be part of the root install
				continue;
			}
			string BundleCloudAppName = SanitizeCloudAppName(Bundle.CloudAppNameOverride, BaseAppName);
			if (!CloudAppsToDependencies.TryGetValue(BundleCloudAppName, out HashSet<string> CloudAppDependencies))
			{
				CloudAppDependencies = new HashSet<string>();
				CloudAppsToDependencies.Add(BundleCloudAppName, CloudAppDependencies);
			}

			IEnumerable<BundleUtils.BundleSettings> BundleDependencies = BundleUtils.GetBundleDependencies(Bundle, Bundles);
			foreach (BundleUtils.BundleSettings BundleDependency in BundleDependencies)
			{
				if (BundleDependency.Name == Bundle.Name)
				{
					// Do not allow a bundle to depend on itself
					continue;
				}
				CloudAppDependencies.Add(SanitizeCloudAppName(BundleDependency.CloudAppNameOverride, BaseAppName));
			}
		}

		foreach (KeyValuePair<string, HashSet<string>> DependencySet in CloudAppsToDependencies)
		{
			string CloudAppName = DependencySet.Key;
			if (!TemplateTree.Aliases.ContainsKey(CloudAppName))
			{
				TemplateTree.Aliases.Add(CloudAppName, DependencySet.Key);
			}
			if (!TemplateTree.Entries.TryGetValue(CloudAppName, out Entry CloudAppEntry))
			{
				CloudAppEntry = new Entry()
				{
					Namespace = "",
					CatalogItemId = "",
					Dependencies = DependencySet.Value
				};
				TemplateTree.Entries.Add(CloudAppName, CloudAppEntry);
			}
		}

		return TemplateTree;
	}

	public JsonObject ToJson()
	{
		JsonObject Json = new JsonObject();
		JsonObject AliasesObject = new JsonObject();
		JsonObject EntriesObject = new JsonObject();
		foreach (KeyValuePair<string, string> Alias in Aliases)
		{
			AliasesObject.AddOrSetFieldValue(Alias.Key, Alias.Value);
		}
		foreach (KeyValuePair<string, Entry> Node in Entries)
		{
			EntriesObject.AddOrSetFieldValue(Node.Key, Node.Value.ToJson());
		}
		Json.AddOrSetFieldValue("aliases", AliasesObject);
		Json.AddOrSetFieldValue("entries", EntriesObject);
		return Json;
	}

	public bool FromJson(JsonObject Json)
	{
		if (Json.TryGetObjectArrayField("elements", out JsonObject[] Elements))
		{
			// If there is an elements field we are processing a list of trees from the backend, find the latest revision
			int LatestRevision = 0; // Note that revisions start at 1
			JsonObject LatestTree = null;
			foreach (JsonObject Element in Elements)
			{
				if (!Element.TryGetIntegerField("rvn", out int Revision))
				{
					continue;
				}
				if (Revision > LatestRevision)
				{
					LatestRevision = Revision;
					LatestTree = Element;
				}
			}

			if (LatestTree != null)
			{
				// Just target the latest tree, ignore the other entries
				Json = LatestTree;
			}
			else
			{
				// We failed to find any trees or the revision values were busted, fail to parse
				return false;
			}
		}
		if (!Json.TryGetObjectField("aliases", out JsonObject AliasesObject))
		{
			return false;
		}
		if (!Json.TryGetObjectField("entries", out JsonObject EntriesObject))
		{
			return false;
		}
		foreach (string AliasKey in AliasesObject.KeyNames)
		{
			Aliases.Add(AliasKey, AliasesObject.GetStringField(AliasKey));
		}
		foreach (string Artifact in EntriesObject.KeyNames)
		{
			Entry NewEntry = new Entry();
			NewEntry.FromJson(EntriesObject.GetObjectField(Artifact));
			Entries.Add(Artifact, NewEntry);
		}
		return true;
	}

	public override string ToString()
	{
		return ToJson().ToJsonString();
	}

	public static bool TryParse(string JsonText, out CloudAppDependencyTree Tree)
	{
		Tree = null;
		if (!JsonObject.TryParse(JsonText, out JsonObject Json))
		{
			return false;
		}
		CloudAppDependencyTree Result = new CloudAppDependencyTree();
		if (!Result.FromJson(Json))
		{
			return false;
		}
		Tree = Result;
		return true;
	}
}