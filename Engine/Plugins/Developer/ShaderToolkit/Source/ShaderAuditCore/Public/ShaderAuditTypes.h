// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "ShaderAuditCore.h"

struct FShaderAuditSession;

// ============================================================================
// Shared data types used by session, widgets, and MCP commands.
// ============================================================================

/** A node in the shader cost hierarchy. Folders and assets share this structure. */
struct FShaderFolderNode
{
	FString Name;
	FString FullPath;		// "/" for root, "/Game/MyContent" for folders, "/Game/MyContent/Foo.Bar" for assets
	FString TooltipText;	// Shown on hover (e.g. real UE package path in hierarchy mode)
	FString EditorPath;		// Non-empty for generated sub-object nodes and their authored proxy folder.
							// Loadable UE object path of the source authored MIC
							// (e.g. "/Game/.../Meatball_Parts.Meatball_Parts").
							// Use this for MaterialValidation / ResolveMaterialPath instead of FullPath.
	FString ClassName;		// Class name for assets (empty for folders)
	bool bIsAsset = false;
	int32 MaterialIndex = INDEX_NONE;	// Index into FShaderAuditSession::UniqueMaterials (-1 for folders)
	int32 ShaderCount = 0;	// For assets: direct count. For folders: sum of all descendants.
	float Cost = 0.f;		// For assets: sum(1/refcount). For folders: sum of all descendants.

	TArray<TSharedPtr<FShaderFolderNode>> Children;
	TWeakPtr<FShaderFolderNode> Parent;

	bool operator==(const FShaderFolderNode& Other) const { return FullPath == Other.FullPath; }
};

/** A row in the detail panel (top-N assets or per-shader breakdown) */
struct FShaderDetailRow
{
	FString Label;
	FString ClassName;
	int32 ShaderCount = 0;
	float Cost = 0.f;			// Shaders / RefCount
	FString AssetPath; // Display path for tooltips (empty for shader detail rows)
	int32 MaterialIndex = INDEX_NONE; // For navigation to asset node (INDEX_NONE for shader rows)
	FName ShaderType;
	FName VFType;
	FName TargetFrequency;
	FName PermutationId;
	FString HashString;
	int32 RefCount = 0;
	int32 Size = 0;				// Shader code size in bytes (0 = unknown)
	bool bSizeWeighted = false;	// true when costs are in bytes (BytecodeDatabase attached)
};

/** Build detail rows for a material's shaders. Shared between Slate detail panel and MCP.
 *  Filters by VisibleShaders bitarray, sorts by size descending then refcount ascending. */
SHADERAUDITCORE_API TArray<TSharedPtr<FShaderDetailRow>> BuildMaterialDetailRows(
	const FShaderAuditSession& Session,
	int32 MaterialIndex,
	const TBitArray<>* VisibleShaders);

namespace UE::ShaderAudit::Utils
{
	/** Format a byte count as human-readable (B / KB / MB / GB). */
	inline FString FormatBytes(uint64 Bytes)
	{
		if (Bytes < 1024)
		{
			return FString::Printf(TEXT("%llu B"), Bytes);
		}
		if (Bytes < 1024 * 1024)
		{
			return FString::Printf(TEXT("%.1f KB"), Bytes / 1024.0);
		}
		if (Bytes < 1024ull * 1024 * 1024)
		{
			return FString::Printf(TEXT("%.1f MB"), Bytes / (1024.0 * 1024.0));
		}
		return FString::Printf(TEXT("%.2f GB"), Bytes / (1024.0 * 1024.0 * 1024.0));
	}

	/**
	 * Format a cost value for display. When a BytecodeDatabase is attached, costs
	 * are size-weighted (bytes) and should be shown as KB/MB. Otherwise they are
	 * simple refcount-weighted counts shown as plain numbers.
	 */
	inline FString FormatCost(float Cost, bool bSizeWeighted)
	{
		if (bSizeWeighted)
		{
			return FormatBytes(static_cast<uint64>(Cost));
		}
		return FString::Printf(TEXT("%.0f"), Cost);
	}
}
