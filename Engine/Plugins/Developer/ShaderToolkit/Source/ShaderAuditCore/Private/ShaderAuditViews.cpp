// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderAuditViews.h"
#include "ShaderAuditSession.h"
#include "ShaderAuditTypes.h"
#include "ITreeMap.h"

// ============================================================================
// Views -- free functions operating on FShaderAuditSession
// ============================================================================

namespace UE::ShaderAudit
{

static TSharedPtr<FShaderFolderNode> GetOrCreateFolder(const FString& FolderPath, const TSharedRef<FShaderFolderNode>& SubtreeRoot, const FString& Prefix, TMap<FString, TSharedPtr<FShaderFolderNode>>& OutNodeMap)
{
	if (FolderPath.IsEmpty() || FolderPath == TEXT("/"))
	{
		return SubtreeRoot;
	}

	FString MapKey = Prefix + FolderPath;
	if (TSharedPtr<FShaderFolderNode>* Existing = OutNodeMap.Find(MapKey))
	{
		return *Existing;
	}

	TArray<FString> Parts;
	FolderPath.ParseIntoArray(Parts, TEXT("/"), true);

	TSharedPtr<FShaderFolderNode> CurrentParent = SubtreeRoot;
	FString CurrentPath = Prefix;

	for (const FString& Part : Parts)
	{
		CurrentPath += TEXT("/");
		CurrentPath += Part;

		if (TSharedPtr<FShaderFolderNode>* ExistingFolder = OutNodeMap.Find(CurrentPath))
		{
			CurrentParent = *ExistingFolder;
		}
		else
		{
			TSharedPtr<FShaderFolderNode> NewFolder = MakeShared<FShaderFolderNode>();
			NewFolder->Name = Part;
			NewFolder->FullPath = CurrentPath;
			NewFolder->Parent = CurrentParent;
			CurrentParent->Children.Add(NewFolder);
			OutNodeMap.Add(CurrentPath, NewFolder);
			CurrentParent = NewFolder;
		}
	}

	return CurrentParent;
}

// ============================================================================
// PopulateFolderSubtree -- shared folder-tree builder
// ============================================================================

static void PopulateFolderSubtree(
	const FShaderAuditSession& Session,
	const TSharedRef<FShaderFolderNode>& SubtreeRoot,
	const TBitArray<>* VisibleShaders,
	bool bHierarchyMode,
	TMap<FString, TSharedPtr<FShaderFolderNode>>& OutNodeMap)
{

	// Prefix all folder paths with SubtreeRoot->FullPath so the node map stays unique
	// when multiple subtrees coexist (e.g. hierarchy roots + "Other").
	FString Prefix = SubtreeRoot->FullPath;
	Prefix.RemoveFromEnd(TEXT("/"));

	// Get or create a folder node, creating parent folders as needed
	const bool bSizeWeighted = Session.HasBytecodeDatabase();

	// In hierarchy mode, cache the built paths so we don't walk the parent chain repeatedly.
	// Materials without a resolved package fall back to their folder path.
	TArray<FString> HierarchyPaths;
	if (bHierarchyMode)
	{
		HierarchyPaths.SetNum(Session.UniqueMaterials.Num());
		for (int32 i = 0; i < Session.UniqueMaterials.Num(); ++i)
		{
			const FShaderAuditSession::FUniqueMaterial& Mat = Session.UniqueMaterials[i];
			if (Mat.PackageIndex != INDEX_NONE)
			{
				HierarchyPaths[i] = Session.BuildHierarchyPath(i);
			}
			else
			{
				// Unresolved: fall back to folder path under an appropriate root
				HierarchyPaths[i] = Mat.Path;
			}
		}
	}

	for (int32 i = 0; i < Session.UniqueMaterials.Num(); ++i)
	{
		const int32 MatIdx = i;
		const FString& AssetPath = bHierarchyMode ? HierarchyPaths[MatIdx] : Session.UniqueMaterials[MatIdx].Path;
		const TArray<int32>& ShaderIndices = Session.UniqueMaterials[MatIdx].ShaderIndices;

		// Count visible shaders and compute cost (deduplicate by OutputHash)
		int32 ShaderCount = 0;
		float AssetCost = 0.f;
		TSet<FShaderHash> CountedHashes;

		for (int32 Idx : ShaderIndices)
		{
			if (VisibleShaders && !(*VisibleShaders)[Idx])
			{
				continue;
			}

			++ShaderCount;

			const FShaderHash& Hash = Session.StableShaderKeyAndValueArray[Idx].OutputHash;

			// Only count each unique hash once per material for cost attribution
			bool bAlreadyInSet = false;
			CountedHashes.Add(Hash, &bAlreadyInSet);
			if (bAlreadyInSet)
			{
				continue;
			}

			const int32 RC = Session.GetHashRefCount(Hash);
			if (RC > 0)
			{
				float Weight = 1.f;
				if (bSizeWeighted)
				{
					uint8 Frequency = 0;
					uint32 CompressedSize = 0;
					uint32 UncompressedSize = 0;
					if (Session.GetShaderBytecodeInfo(Hash, Frequency, CompressedSize, UncompressedSize))
					{
						Weight = static_cast<float>(CompressedSize);
					}
				}
				AssetCost += Weight / static_cast<float>(RC);
			}
		}

		if (ShaderCount == 0)
		{
			continue;
		}

		// Split into folder and asset name
		int32 LastSlash = INDEX_NONE;
		AssetPath.FindLastChar(TEXT('/'), LastSlash);

		FString FolderPath;
		FString AssetName;
		if (LastSlash != INDEX_NONE)
		{
			FolderPath = AssetPath.Left(LastSlash);
			AssetName = AssetPath.Mid(LastSlash + 1);
		}
		else
		{
			AssetName = AssetPath;
		}

		TSharedPtr<FShaderFolderNode> FolderNode = GetOrCreateFolder(FolderPath, SubtreeRoot, Prefix, OutNodeMap);

		TSharedPtr<FShaderFolderNode> AssetNode = MakeShared<FShaderFolderNode>();
		AssetNode->Name = AssetName;
		AssetNode->ClassName = Session.UniqueMaterials[MatIdx].ClassName;
		AssetNode->bIsAsset = true;
		AssetNode->MaterialIndex = MatIdx;
		AssetNode->ShaderCount = ShaderCount;
		AssetNode->Cost = AssetCost;
		AssetNode->Parent = FolderNode;
		AssetNode->FullPath = AssetPath;
		FolderNode->Children.Add(AssetNode);

		OutNodeMap.Add(AssetPath, AssetNode);

		// In hierarchy mode, set clean display names and package paths for tooltips.
		// FullPath has _N suffixes for uniqueness; Name should be the clean leaf name.
		if (bHierarchyMode && Session.UniqueMaterials[MatIdx].PackageIndex != INDEX_NONE)
		{
			const int32 PkgIdx = Session.UniqueMaterials[MatIdx].PackageIndex;

			// Asset node: display the clean asset name, tooltip shows the real path.
			// For sub-object materials (item wraps, Niagara scripts), show
			// "OuterMI.SubObjectType" instead of the internal sub-object container name.
			AssetNode->Name = FPaths::GetCleanFilename(Session.UniqueMaterials[MatIdx].Path);
			AssetNode->TooltipText = Session.UniqueMaterials[MatIdx].Path;

			{
				FString OuterPkg, OuterLeaf, SubObjLeaf;
				if (FShaderAuditSession::DetectSubObject(Session.UniqueMaterials[MatIdx].Path, OuterPkg, OuterLeaf, SubObjLeaf))
				{
					AssetNode->Name = OuterLeaf + TEXT(".") + SubObjLeaf;
					AssetNode->EditorPath = OuterPkg + TEXT(".") + OuterLeaf;
				}
			}

			// Walk up to fix folder nodes (packages in the parent chain)
			TSharedPtr<FShaderFolderNode> FolderWalk = FolderNode;
			int32 PkgWalk = PkgIdx;
			while (FolderWalk.IsValid() && FolderWalk != SubtreeRoot && PkgWalk != INDEX_NONE)
			{
				if (FolderWalk->TooltipText.IsEmpty())
				{
					FolderWalk->Name = FPaths::GetCleanFilename(Session.MaterialPackages[PkgWalk].PackagePath);
					FolderWalk->TooltipText = Session.MaterialPackages[PkgWalk].PackagePath;
					FolderWalk->EditorPath = Session.MaterialPackages[PkgWalk].PackagePath + TEXT(".") + FPaths::GetPathLeaf(Session.MaterialPackages[PkgWalk].PackagePath);
				}
				FolderWalk = FolderWalk->Parent.Pin();
				PkgWalk = Session.MaterialPackages[PkgWalk].ParentIndex;
			}
		}
	}

	// Roll up ShaderCount/Cost and prune empty folders
	TFunction<int32(TSharedPtr<FShaderFolderNode>)> RollUp =
		[&RollUp](TSharedPtr<FShaderFolderNode> Node) -> int32
	{
		if (Node->Children.Num() == 0)
		{
			return Node->ShaderCount;
		}

		Node->Children.RemoveAll([&RollUp](const TSharedPtr<FShaderFolderNode>& Child)
		{
			int32 Count = RollUp(Child);
			return Count == 0;
		});

		int32 Total = 0;
		float TotalCost = 0.f;
		for (const auto& Child : Node->Children)
		{
			Total += Child->ShaderCount;
			TotalCost += Child->Cost;
		}

		// For assets that also have children (e.g. base material in hierarchy view),
		// keep own shaders and add children on top.
		Node->ShaderCount += Total;
		Node->Cost += TotalCost;

		return Node->ShaderCount;
	};
	RollUp(SubtreeRoot);
}

// ============================================================================
// BuildFolderTree
// ============================================================================

TSharedRef<FShaderFolderNode> BuildFolderTree(
	const FShaderAuditSession& Session,
	const TBitArray<>* VisibleShaders,
	TMap<FString, TSharedPtr<FShaderFolderNode>>& OutNodeMap)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(BuildFolderTree);

	OutNodeMap.Empty();

	TSharedRef<FShaderFolderNode> RootNode = MakeShared<FShaderFolderNode>();
	RootNode->Name = TEXT("/");
	RootNode->FullPath = TEXT("/");
	OutNodeMap.Add(TEXT("/"), RootNode);

	PopulateFolderSubtree(Session, RootNode, VisibleShaders, false, OutNodeMap);

	return RootNode;
}

// ============================================================================
// BuildMaterialHierarchyTree
// ============================================================================

TSharedRef<FShaderFolderNode> BuildMaterialHierarchyTree(
	const FShaderAuditSession& Session,
	const TBitArray<>* VisibleShaders,
	TMap<FString, TSharedPtr<FShaderFolderNode>>& OutNodeMap)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(BuildMaterialHierarchyTree);

	OutNodeMap.Empty();

	TSharedRef<FShaderFolderNode> RootNode = MakeShared<FShaderFolderNode>();
	RootNode->Name = TEXT("/");
	RootNode->FullPath = TEXT("/");
	OutNodeMap.Add(TEXT("/"), RootNode);

	PopulateFolderSubtree(Session, RootNode, VisibleShaders, true, OutNodeMap);
	return RootNode;
}

/** Format a cost value -- size-weighted shows as KB/MB, otherwise plain number. */
static FString FormatCostForTreeMap(float Cost, bool bSizeWeighted)
{
	if (bSizeWeighted)
	{
		if (Cost < 1024.f)
		{
			return FString::Printf(TEXT("%.0f B"), Cost);
		}
		if (Cost < 1024.f * 1024.f)
		{
			return FString::Printf(TEXT("%.1f KB"), Cost / 1024.f);
		}
		if (Cost < 1024.f * 1024.f * 1024.f)
		{
			return FString::Printf(TEXT("%.1f MB"), Cost / (1024.f * 1024.f));
		}
		return FString::Printf(TEXT("%.2f GB"), Cost / (1024.f * 1024.f * 1024.f));
	}
	return FString::Printf(TEXT("%.0f"), Cost);
}

TSharedRef<FTreeMapNodeData> BuildTreeMapView(
	const TSharedRef<FShaderFolderNode>& Root,
	int32 MaxDepth,
	bool bSizeWeighted)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(BuildTreeMapView);

	struct Local
	{
		static TSharedPtr<FTreeMapNodeData> Build(
			const TSharedPtr<FShaderFolderNode>& FolderNode,
			FTreeMapNodeData* ParentTM,
			int32 CurrentDepth,
			int32 MaxDepth,
			bool bSizeWeighted)
		{
			if (!FolderNode.IsValid() || FolderNode->Cost <= 0.f)
			{
				return nullptr;
			}

			TSharedPtr<FTreeMapNodeData> TMNode = MakeShared<FTreeMapNodeData>();
			TMNode->Name = FolderNode->Name;
			TMNode->LogicalName = FolderNode->FullPath;
			TMNode->Parent = ParentTM;
			TMNode->Size = FolderNode->Cost;

			if (MaxDepth > 0 && CurrentDepth >= MaxDepth)
			{
				TMNode->CenterText = FormatCostForTreeMap(FolderNode->Cost, bSizeWeighted);
				//TMNode->Color = FLinearColor(0.35f, 0.45f, 0.55f, 1.0f);
				return TMNode;
			}

			if (FolderNode->MaterialIndex != INDEX_NONE)
			{
				if (FolderNode->Children.Num() == 0)
				{
					// Leaf asset -- no children to recurse into
					TMNode->CenterText = FormatCostForTreeMap(FolderNode->Cost, bSizeWeighted);
					TMNode->Name2 = FolderNode->ClassName;

					if (FolderNode->ClassName == TEXT("MaterialInstanceConstant") || FolderNode->ClassName == TEXT("MaterialInstance") || FolderNode->ClassName == TEXT("MaterialInstanceDynamic"))
					{
						//TMNode->Color = FLinearColor(0.2f, 0.6f, 0.9f, 1.0f);
					}
					else if (FolderNode->ClassName == TEXT("Material"))
					{
						//TMNode->Color = FLinearColor(0.9f, 0.3f, 0.2f, 1.0f);
					}
					else if (FolderNode->ClassName.Contains(TEXT("Landscape")))
					{
						//TMNode->Color = FLinearColor(0.3f, 0.8f, 0.3f, 1.0f);
					}
					else
					{
						//TMNode->Color = FLinearColor(0.6f, 0.4f, 0.7f, 1.0f);
					}
				}
				else
				{
					// Asset with children (e.g. base Material in hierarchy view) -- recurse
					TMNode->Name2 = FolderNode->ClassName;
					//TMNode->Color = FLinearColor(0.9f, 0.3f, 0.2f, 1.0f);

					for (const auto& Child : FolderNode->Children)
					{
						TSharedPtr<FTreeMapNodeData> ChildTM = Build(Child, TMNode.Get(), CurrentDepth + 1, MaxDepth, bSizeWeighted);
						if (ChildTM.IsValid())
						{
							TMNode->Children.Add(ChildTM);
						}
					}

					if (TMNode->Children.Num() == 0)
					{
						// All children were pruned -- show as leaf
						TMNode->CenterText = FormatCostForTreeMap(FolderNode->Cost, bSizeWeighted);
					}
				}
			}
			else
			{
				//TMNode->Color = FLinearColor(0.2f, 0.3f, 0.5f, 1.0f);

				for (const auto& Child : FolderNode->Children)
				{
					TSharedPtr<FTreeMapNodeData> ChildTM = Build(Child, TMNode.Get(), CurrentDepth + 1, MaxDepth, bSizeWeighted);
					if (ChildTM.IsValid())
					{
						TMNode->Children.Add(ChildTM);
					}
				}

				if (TMNode->Children.Num() == 0 && TMNode->Size == 0.f)
				{
					return nullptr;
				}
			}

			return TMNode;
		}
	};

	TSharedRef<FTreeMapNodeData> RootTM = MakeShared<FTreeMapNodeData>();
	RootTM->Name = Root->Name;
	RootTM->LogicalName = Root->FullPath;
	RootTM->Color = FLinearColor(0.2f, 0.3f, 0.5f, 1.0f);
	RootTM->Size = Root->Cost;
	RootTM->CenterText = FormatCostForTreeMap(Root->Cost, bSizeWeighted);

	for (const auto& Child : Root->Children)
	{
		TSharedPtr<FTreeMapNodeData> ChildTM = Local::Build(Child, RootTM.operator->(), 1, MaxDepth, bSizeWeighted);
		if (ChildTM.IsValid())
		{
			RootTM->Children.Add(ChildTM);
		}
	}

	return RootTM;
}

} // namespace UE::ShaderAudit
