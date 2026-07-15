// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderAuditSession.h"

#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogShaderAuditAssetRegistry, Log, All);


// Detect sub-object materials produced by FCompactFullName::AppendString flattening.
// In UE, the package leaf and root object always share the same name (e.g. MI_Name/MI_Name).
// When a sub-object exists (Package.Object:SubObj.Leaf), AppendString flattens all separators
// to '/' except the last which becomes '.', producing: .../MI_Name/MI_Name/SubObjContainer.Leaf
// The consecutive duplicate component is the detection signal.
bool FShaderAuditSession::DetectSubObject(const FString& Path, FString& OutOuterPackage, FString& OutOuterLeafName, FString& OutSubObjectLeaf)
{
	// Extract the sub-object leaf (after the last dot)
	int32 LastDotIdx;
	if (!Path.FindLastChar(TEXT('.'), LastDotIdx) || LastDotIdx == 0)
	{
		return false;
	}
	const FString LeafAfterDot = Path.Mid(LastDotIdx + 1);
	const FString BeforeDot = Path.Left(LastDotIdx);

	// Split the path before the dot by '/' and look for consecutive duplicate components
	TArray<FString> Components;
	BeforeDot.ParseIntoArray(Components, TEXT("/"), true);

	for (int32 i = Components.Num() - 1; i >= 1; --i)
	{
		if (Components[i] == Components[i - 1])
		{
			// Found the duplicate: everything up to (and including) the first occurrence
			// is the outer package path. Scanning backward finds the rightmost duplicate first,
			// which is correct for real sub-objects. However, this can false-positive on paths
			// with coincidental duplicate folder names (e.g. /Game/Maps/Maps/Foo.Bar).
			// Callers should gate with asset registry data when available (see SetupMaterialParents).
			FString OuterPkg;
			for (int32 j = 0; j < i; ++j)
			{
				OuterPkg += TEXT("/");
				OuterPkg += Components[j];
			}
			OutOuterPackage = OuterPkg;
			OutOuterLeafName = Components[i];
			OutSubObjectLeaf = LeafAfterDot;
			return true;
		}
	}

	return false;
}

void FShaderAuditSession::SetupMaterialParents(const TMap<FString, FString>& ParentMap)
{
	TMap<FString, int32> PackagePathToIndex;

	// Populate the list of packages from what we had from the Asset registry
	{
		MaterialPackages.Reset();
		MaterialPackages.Reserve(ParentMap.Num());
		for (const TPair<FString, FString>& Pair : ParentMap)
		{
			const FString& ChildPkg = Pair.Key;
			const FString& ParentPkg = Pair.Value;

			const int32 ChildIdx = PackagePathToIndex.FindOrAdd(ChildPkg, MaterialPackages.Num());
			if (ChildIdx == MaterialPackages.Num())
			{
				FMaterialPackage& Pkg = MaterialPackages.AddDefaulted_GetRef();
				Pkg.PackagePath = ChildPkg;
			}

			if (!ParentPkg.IsEmpty())
			{
				const int32 ParentIdx = PackagePathToIndex.FindOrAdd(ParentPkg, MaterialPackages.Num());
				if (ParentIdx == MaterialPackages.Num())
				{
					FMaterialPackage& Pkg = MaterialPackages.AddDefaulted_GetRef();
					Pkg.PackagePath = ParentPkg;
				}
				MaterialPackages[ChildIdx].ParentIndex = ParentIdx;
			}
		}

		NumMaterialParents = ParentMap.Num();

	}

	// Find asset => package connection
	int32 NumMatched = 0;
	{
		for (int32 MatIdx = 0; MatIdx < UniqueMaterials.Num(); ++MatIdx)
		{
			FString MaterialPackageName = FPackageName::ObjectPathToPackageName(UniqueMaterials[MatIdx].Path);
			const int32* PkgIdx = PackagePathToIndex.Find(MaterialPackageName);
			if (PkgIdx == nullptr)
			{
				FString OuterLeaf, SubObjLeaf;
				if (DetectSubObject(UniqueMaterials[MatIdx].Path, MaterialPackageName, OuterLeaf, SubObjLeaf))
				{
					PkgIdx = PackagePathToIndex.Find(MaterialPackageName);
				}
			}

			if (PkgIdx)
			{
				UniqueMaterials[MatIdx].PackageIndex = *PkgIdx;
				++NumMatched;
			}
			else
			{
				UE_LOGF(LogShaderAuditAssetRegistry, Warning, "Material hierarchy: Uable to find package %ls for %ls", *MaterialPackageName, *UniqueMaterials[MatIdx].Path)
			}
		}
	}


	const float Pct = UniqueMaterials.Num() > 0
		? 100.f * static_cast<float>(NumMatched) / static_cast<float>(UniqueMaterials.Num())
		: 0.f;

	UE_LOGF(LogShaderAuditAssetRegistry, Display,
		"Material hierarchy: %d packages, %d parent links matched (%.1f%% of %d materials)",
		MaterialPackages.Num(), NumMatched, Pct, UniqueMaterials.Num());
}

FString FShaderAuditSession::BuildHierarchyPath(int32 MaterialIndex) const
{
	check(UniqueMaterials.IsValidIndex(MaterialIndex));

	const int32 PkgIdx = UniqueMaterials[MaterialIndex].PackageIndex;
	if (PkgIdx == INDEX_NONE)
	{
		return FString();
	}

	// Walk parent chain, collect package indices bottom-up.
	// Use a visited set to detect cyclic parent chains (corrupt data).
	TArray<int32, TInlineAllocator<8>> Chain;
	TBitArray<> Visited;
	Visited.Init(false, MaterialPackages.Num());
	int32 Current = PkgIdx;
	while (Current != INDEX_NONE)
	{
		check(MaterialPackages.IsValidIndex(Current));
		if (Visited[Current])
		{
			UE_LOGF(LogShaderAuditAssetRegistry, Warning,
				"Cyclic parent chain detected at package index %d (%ls) -- truncating hierarchy path",
				Current, *MaterialPackages[Current].PackagePath);
			break;
		}
		Visited[Current] = true;
		Chain.Add(Current);
		Current = MaterialPackages[Current].ParentIndex;
	}

	// Reverse to get root-first ordering
	Algo::Reverse(Chain);

	// Build the path using package indices for guaranteed uniqueness.
	// Display names are set separately on FShaderFolderNode by PopulateFolderSubtree.
	// Format: /LeafName_PkgIdx/LeafName_PkgIdx/LeafName_MatIdx
	FString Result;
	for (int32 i = 0; i < Chain.Num(); ++i)
	{
		const FString LeafName = FPaths::GetPathLeaf(MaterialPackages[Chain[i]].PackagePath);
		Result += TEXT("/");
		Result += LeafName;
		Result += TEXT("_");
		Result.AppendInt(Chain[i]);
	}

	// Leaf: use material index to distinguish materials sharing a package
	Result += TEXT("/");
	Result += FPaths::GetPathLeaf(UniqueMaterials[MaterialIndex].Path);
	Result += TEXT("_");
	Result.AppendInt(MaterialIndex);

	return Result;
}
