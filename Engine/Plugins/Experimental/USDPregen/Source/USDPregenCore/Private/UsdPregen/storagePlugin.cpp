// Copyright Epic Games, Inc. All Rights Reserved.

#if USE_USD_SDK

#include "UsdPregen/storagePlugin.h"

#include "UsdPregen/extAssetDefinition.h"
#include "UsdPregen/manifestTypes.h"
#include "UsdPregen/target.h"

#include "USDIncludesStart.h"
#include "pxr/pxr.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/dictionary.h"
#include "pxr/base/vt/value.h"
#include "USDIncludesEnd.h"

#include <cctype>
#include <optional>
#include <sstream>
#include <string>

PREGEN_NAMESPACE_USING_DIRECTIVE

PXR_NAMESPACE_USING_DIRECTIVE

PREGEN_NAMESPACE_OPEN_SCOPE

namespace
{
	std::string SanitizePackageSegment(const std::string& inValue)
	{
		std::string result;
		result.reserve(inValue.size());

		for (char character : inValue)
		{
			const unsigned char unsignedChar = static_cast<unsigned char>(character);
			if (std::isalnum(unsignedChar) || character == '_')
			{
				result.push_back(character);
			}
			else
			{
				result.push_back('_');
			}
		}

		if (result.empty())
		{
			return StoragePlugin::EmptyValueSentinel();
		}
		return result;
	}

	// Convert a leaf VtValue into a string suitable for substitution into a
	// path segment. Non-scalar values (dictionaries, arrays) intentionally
	// produce an empty string so the sanitizer collapses them to the
	// sentinel rather than dumping a stringified dict like "{a: 1}" into
	// the path.
	std::string StringifyMetadataValue(const pxr::VtValue& value)
	{
		if (value.IsEmpty())
		{
			return {};
		}
		if (value.IsHolding<pxr::VtDictionary>())
		{
			return {};
		}
		if (value.IsArrayValued())
		{
			return {};
		}
		if (value.IsHolding<std::string>())
		{
			return value.UncheckedGet<std::string>();
		}
		if (value.IsHolding<pxr::TfToken>())
		{
			return value.UncheckedGet<pxr::TfToken>().GetString();
		}

		// Numeric, bool, asset paths, etc.: lean on the registered stream
		// operator. For the types pregen typically sees in assetInfo this
		// yields a sensible representation (e.g. "5", "true", "1.5").
		std::ostringstream os;
		os << value;
		return os.str();
	}

	// Walk a colon-separated path like "category" or "nested:subcategory"
	// through `rootDict`, descending into nested VtDictionary values.
	// Returns the leaf VtValue if the full path resolves, else nullopt.
	std::optional<pxr::VtValue> LookupMetadataPath(
		const pxr::VtDictionary& rootDict,
		const std::string& path)
	{
		if (path.empty())
		{
			return std::nullopt;
		}

		const pxr::VtDictionary* currentDict = &rootDict;
		std::size_t segmentStart = 0;
		while (segmentStart <= path.size())
		{
			std::size_t separator = path.find(':', segmentStart);
			if (separator == std::string::npos)
			{
				separator = path.size();
			}
			const std::string segment = path.substr(segmentStart, separator - segmentStart);
			if (segment.empty())
			{
				return std::nullopt;
			}

			if (!currentDict)
			{
				return std::nullopt;
			}

			const auto entry = currentDict->find(segment);
			if (entry == currentDict->end())
			{
				return std::nullopt;
			}

			const bool isLastSegment = (separator >= path.size());
			if (isLastSegment)
			{
				return entry->second;
			}

			// Not the last segment: must descend into a nested dict.
			if (entry->second.IsHolding<pxr::VtDictionary>())
			{
				currentDict = &entry->second.UncheckedGet<pxr::VtDictionary>();
			}
			else
			{
				return std::nullopt;
			}
			segmentStart = separator + 1;
		}

		return std::nullopt;
	}

	// Look up `key` in extraSubstitutions first, then in the built-in
	// placeholder map derived from targetUid/definitions/assetType. Returns
	// the value sanitized for use as a package-path segment.
	std::string ResolvePlaceholder(
		const std::string& key,
		const TargetUid& targetUid,
		const ExtAssetDefinition* leafDefinition,
		const std::string& assetType,
		const std::unordered_map<std::string, std::string>& extraSubstitutions)
	{
		const auto extraIt = extraSubstitutions.find(key);
		if (extraIt != extraSubstitutions.end())
		{
			return SanitizePackageSegment(extraIt->second);
		}

		if (key == "DEFINITION_NAME")
		{
			return SanitizePackageSegment(leafDefinition ? leafDefinition->GetName() : std::string{});
		}
		if (key == "DEFINITION_VERSION")
		{
			return SanitizePackageSegment(leafDefinition ? leafDefinition->GetVersion() : std::string{});
		}
		if (key == "DEFINITION_UID")
		{
			return SanitizePackageSegment(leafDefinition ? leafDefinition->GetUniqueId() : std::string{});
		}
		if (key == "PERMUTATION_ID")
		{
			return SanitizePackageSegment(targetUid.GetPermutationUid());
		}
		if (key == "ASSET_TYPE")
		{
			return SanitizePackageSegment(assetType);
		}

		// ${METADATA:foo} or ${METADATA:foo:bar} -> walk the leaf
		// definition's metadata dictionary (populated from USD assetInfo,
		// with built-in keys stripped, by the default discovery plugin's
		// _AttachCustomAssetInfoMetadata). Nested dicts are descended via
		// additional ":subkey" segments.
		static const std::string metadataPrefix = "METADATA:";
		if (key.rfind(metadataPrefix, 0) == 0)
		{
			if (!leafDefinition || !leafDefinition->HasMetadata())
			{
				return StoragePlugin::EmptyValueSentinel();
			}

			const std::string metadataPath = key.substr(metadataPrefix.size());
			const std::optional<pxr::VtValue> resolved = LookupMetadataPath(
				*leafDefinition->GetMetadata(), metadataPath);
			if (!resolved.has_value())
			{
				return StoragePlugin::EmptyValueSentinel();
			}

			return SanitizePackageSegment(StringifyMetadataValue(*resolved));
		}

		// Unknown placeholders collapse to the sentinel (rather than being
		// left literal) so a typo doesn't produce a path with a stray
		// "${TYPO}" segment.
		return StoragePlugin::EmptyValueSentinel();
	}

	// Collapse "//" -> "/" and trim a single trailing slash.
	void CollapseSlashesAndTrim(std::string& path)
	{
		std::string collapsed;
		collapsed.reserve(path.size());
		bool previousWasSlash = false;
		for (char character : path)
		{
			if (character == '/')
			{
				if (previousWasSlash)
				{
					continue;
				}
				previousWasSlash = true;
			}
			else
			{
				previousWasSlash = false;
			}
			collapsed.push_back(character);
		}

		if (!collapsed.empty() && collapsed.back() == '/')
		{
			collapsed.pop_back();
		}

		path = std::move(collapsed);
	}
}	 // namespace

StoragePlugin::StoragePlugin(const StorageOptions& options)
	: _options(options)
{
}

// virtual
StoragePlugin::~StoragePlugin() = default;

// virtual
bool
StoragePlugin::Initialize()
{
	return true;
}

// virtual
bool
StoragePlugin::Shutdown()
{
	return true;
}

// virtual
ManifestSaveResult
StoragePlugin::PersistManifestPayload(const TargetUid& targetUid)
{
	return {};
}

const StorageOptions&
StoragePlugin::GetOptions() const
{
	return _options;
}

// static
const std::string&
StoragePlugin::DefaultPackageSubPathTemplate()
{
	static const std::string sTemplate =
		"assets/${DEFINITION_NAME}/versions/${DEFINITION_VERSION}/permutations/${PERMUTATION_ID}";
	return sTemplate;
}

// static
const std::string&
StoragePlugin::EmptyValueSentinel()
{
	static const std::string sSentinel = "_";
	return sSentinel;
}

// static
std::string
StoragePlugin::ResolvePackageSubPathTemplate(
	const std::string& templateStr,
	const TargetUid& targetUid,
	const std::vector<const ExtAssetDefinition*>& definitions,
	const std::string& assetType,
	const std::unordered_map<std::string, std::string>& extraSubstitutions)
{
	const ExtAssetDefinition* leafDefinition
		= definitions.empty() ? nullptr : definitions.back();

	std::string result;
	result.reserve(templateStr.size());

	std::size_t cursor = 0;
	while (cursor < templateStr.size())
	{
		// Look for the opening "${".
		const char character = templateStr[cursor];
		if (character == '$' && (cursor + 1) < templateStr.size() && templateStr[cursor + 1] == '{')
		{
			const std::size_t closeIndex = templateStr.find('}', cursor + 2);
			if (closeIndex != std::string::npos)
			{
				const std::string placeholderKey = templateStr.substr(cursor + 2, closeIndex - (cursor + 2));
				result += ResolvePlaceholder(
					placeholderKey,
					targetUid,
					leafDefinition,
					assetType,
					extraSubstitutions);
				cursor = closeIndex + 1;
				continue;
			}
		}

		// Plain character (or unmatched "${"): copy verbatim.
		result.push_back(character);
		++cursor;
	}

	CollapseSlashesAndTrim(result);
	return result;
}

PREGEN_NAMESPACE_CLOSE_SCOPE

#endif // USE_USD_SDK
