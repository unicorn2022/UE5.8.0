// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderAuditModule.h"
#include "UObject/TopLevelAssetPath.h"

class UMaterialInterface;

namespace UE::ShaderAudit::Utils
{

// ============================================================================
// FMaterialResolverConfig
// Ini-driven game-specific sub-object material resolver.
// Loaded once from <ProjectDir>/Plugins/ShaderAudit/Config/ShaderAudit.ini.
// Callers that only need to resolve paths should use ResolveMaterialPath().
// Callers that need to enumerate SubObjectPrimaryProps (e.g. BatchGetMaterialParents)
// can access the config directly via GetMaterialResolverConfig().
// ============================================================================

struct FMaterialResolverConfig
{
	// [SubObjectResolver] — used by ResolveMaterialPath
	FTopLevelAssetPath AssetUserDataClassPath;

	// [SubObjectPrimaries] — used by BatchGetMaterialParents sub-object discovery
	FTopLevelAssetPath MappingDataClassPath;
	TArray<TPair<FString, FName>> SubObjectPrimaryProps; // LeafName -> PropertyName

	// [ShaderRootClasses] — asset classes that produce shaders independently of the material
	// hierarchy (e.g. UNiagaraSystem). Enumerated as root entries in BatchGetMaterialParents.
	TArray<FTopLevelAssetPath> ExtraRootClasses;

	bool bValid = false;
};

struct FSubObjectPrimary
{
	FString LeafName;
	FString PrimaryPackagePath;
};

struct FMaterialParentEntry
{
	FString Child;
	FString Parent;
};

struct FMaterialParentMapResult
{
	TArray<FMaterialParentEntry> Pairs;
	TArray<FSubObjectPrimary> SubObjectPrimaries;
	int32 TotalMaterialInterfaces = 0;
	int32 MissingParentTag = 0;
};

struct FHlslSection
{
	FString Name;
	FString Hlsl;
};

struct FMaterialInsight
{
	FString Path;
	TArray<FHlslSection> LegacySections;
	TArray<FHlslSection> NewSections;
	bool bUsesNewGenerator = false;
};

struct FMaterialInsightFailure
{
	FString Path;
	FString Reason;
};

struct FMaterialInsightsResult
{
	TArray<FMaterialInsight> Materials;
	TArray<FMaterialInsightFailure> Failed;
	int32 Succeeded = 0;
	int32 FailedCount = 0;
};

/** Returns the singleton resolver config (loaded once on first call). */
SHADERAUDIT_API const FMaterialResolverConfig& GetMaterialResolverConfig();

/**
 * Load a UMaterialInterface from a path string.
 * Handles both regular asset paths and sub-object material paths
 * (e.g. "/Game/Pkg/Asset.Asset:SubObject.LeafName") using the game-specific
 * resolver config from Plugins/ShaderAudit/Config/ShaderAudit.ini.
 * Returns null on failure; sets OutError (if provided) to a reason string.
 */
SHADERAUDIT_API UMaterialInterface* ResolveMaterialPath(const FString& MaterialPath, FString* OutError = nullptr);

/**
 * Walk the Asset Registry and build a material parent map.
 *
 * @param PathFilter  Optional case-sensitive package-path prefix (e.g. "/Game/").
 *                    Empty = no filter.
 */
SHADERAUDIT_API FMaterialParentMapResult BuildMaterialParentMap(const FString& PathFilter = FString());

/**
 * Load materials by path, run the HLSL translator (no shader compilation), and
 * return per-material ShaderStringParameters from FMaterialInsights.
 *
 * @param Paths     Package paths of materials to translate. Duplicates are ignored.
 *                  Capped at 10000 entries per call.
 * @param Platform  Shader platform name (e.g. "PCD3D_SM5"). Empty = current
 *                  editor's max RHI platform.
 */
SHADERAUDIT_API FMaterialInsightsResult ExtractMaterialInsights(
	const TArray<FString>& Paths,
	const FString& Platform = FString());

} // namespace UE::ShaderAudit::Utils
