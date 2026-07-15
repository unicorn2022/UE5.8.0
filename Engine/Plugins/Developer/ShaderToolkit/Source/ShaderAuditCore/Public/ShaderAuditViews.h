// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "ShaderAuditCore.h"

struct FShaderAuditSession;
struct FShaderFolderNode;
class FTreeMapNodeData;

namespace UE::ShaderAudit
{

/**
 * Build a FShaderFolderNode hierarchy from the session, optionally filtered by a bit array.
 * Assets with no visible shaders are excluded. Cost/ShaderCount reflect only visible shaders.
 * @param Session			The session to build from
 * @param VisibleShaders	Optional: one bit per entry, null = all visible
 * @param OutNodeMap		Output: maps FullPath -> node for fast lookup
 * @return Root folder node
 */
SHADERAUDITCORE_API TSharedRef<FShaderFolderNode> BuildFolderTree(
	const FShaderAuditSession& Session,
	const TBitArray<>* VisibleShaders,
	TMap<FString, TSharedPtr<FShaderFolderNode>>& OutNodeMap);

/**
 * Build a FShaderFolderNode hierarchy based on the material parent chain.
 * Base Materials are roots, MIC children nest underneath. Assets without
 * a parent chain (NiagaraScript, etc.) go under an "Other" root, grouped
 * by folder path. Requires MaterialPackages to be populated.
 * @param Session			The session to build from
 * @param VisibleShaders	Optional: one bit per entry, null = all visible
 * @param OutNodeMap		Output: maps FullPath -> node for fast lookup
 * @return Root node (virtual root containing hierarchy roots + Other)
 */
SHADERAUDITCORE_API TSharedRef<FShaderFolderNode> BuildMaterialHierarchyTree(
	const FShaderAuditSession& Session,
	const TBitArray<>* VisibleShaders,
	TMap<FString, TSharedPtr<FShaderFolderNode>>& OutNodeMap);

/**
 * Build a throwaway FTreeMapNodeData tree from a FShaderFolderNode subtree.
 * Reads Cost directly from FShaderFolderNode.
 */
SHADERAUDITCORE_API TSharedRef<FTreeMapNodeData> BuildTreeMapView(
	const TSharedRef<FShaderFolderNode>& Root,
	int32 MaxDepth,
	bool bSizeWeighted = false);

} // namespace UE::ShaderAudit
