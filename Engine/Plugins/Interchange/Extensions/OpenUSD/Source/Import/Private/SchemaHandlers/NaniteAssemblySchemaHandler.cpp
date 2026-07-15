// Copyright Epic Games, Inc. All Rights Reserved.

#if USE_USD_SDK

#include "SchemaHandlers/NaniteAssemblySchemaHandler.h"

#include "InterchangeUsdContext.h"
#include "InterchangeUsdTranslator.h"
#include "InterchangeUsdTraversalInfo.h"
#include "SchemaHandlers/SchemaHandlerUtils.h"
#include "Usd/InterchangeUsdDefinitions.h"
#include "USDErrorUtils.h"
#include "USDNaniteAssemblyUtils.h"
#include "USDPrimConversion.h"
#include "USDTypesConversion.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdTyped.h"

#include "InterchangeSceneNode.h"

#define LOCTEXT_NAMESPACE "USDPrimConversion"

namespace UE::NaniteAssemblySchemaHandler::Private
{
	using namespace UE::Interchange::USD;

	// Helper to traverse a Nanite assembly root subtree (i.e. a prim with NaniteAssemblyRootAPI schema applied)
	class FNaniteAssemblyTraversalHelper
	{
		using FNaniteAssemblyTraversalResult = UsdToUnreal::NaniteAssemblyUtils::FNaniteAssemblyTraversalResult;
		using ENaniteAssemblyMeshType = UsdToUnreal::NaniteAssemblyUtils::ENaniteAssemblyMeshType;
		using EMeshCategory = UsdToUnreal::NaniteAssemblyUtils::EMeshCategory;
		using FPrimPrototypeEntry = UsdToUnreal::NaniteAssemblyUtils::FPrimPrototypeEntry;
		using FMeshEntry = UsdToUnreal::NaniteAssemblyUtils::FMeshEntry;

		UInterchangeUsdContext& UsdContext;

		// Where we store all the data generated from the NaniteAssembly hierarchy
		FHandlerAccumulatedInfo& AccumulatedInfo;

		UInterchangeSceneNode* ParentSceneNode;
		const FString ParentSceneNodeUid;

		// Our root assembly prim with the NaniteAssemblyRootAPI schema applied
		UE::FUsdPrim AssemblyRootPrim;

		// The type of assembly mesh - skeletal or static
		ENaniteAssemblyMeshType AssemblyMeshType;

		// Base skeleton path (required for skeletal mesh assemblies only)
		UE::FSdfPath BaseSkeletonPrimPath;

		// Base skeleton skel-root prim path.
		UE::FSdfPath BaseSkelRootPrimPath;

		// Computed traversal result that we'll attach to the base mesh's TraversalInfo
		// and access again during mesh payload retrieval
		TSharedPtr<FNaniteAssemblyTraversalResult> Result;

		// The FTraversalInfo's for each base/part mesh
		TMap<UE::FSdfPath, FTraversalInfo> TraversalInfoByCachedPrimPath;

		// The USD implicit prototype path to generated mesh uid. We use this to work-around 
		// interchange not currently handling instanced skeletal meshes too well.
		TMap<UE::FSdfPath, FString> ImplicitPrototypePrimPathToCachedMeshUid;

		// In the skeletal mesh case, parts must have the NaniteAssemblySkelBindingAPI applied. 
		TArray<UE::FSdfPath> InvalidPartMeshNoSkelBindingPaths;

	public:

		FNaniteAssemblyTraversalHelper(UInterchangeUsdContext& InUsdContext, FHandlerAccumulatedInfo& InAccumulatedInfo, UInterchangeSceneNode* InParentSceneNode)
			: UsdContext(InUsdContext)
			, AccumulatedInfo(InAccumulatedInfo)
			, ParentSceneNode(InParentSceneNode)
			, ParentSceneNodeUid(ParentSceneNode->GetUniqueID())
		{

		}

	private:

		bool MeshTypeIs(ENaniteAssemblyMeshType MeshType) const
		{
			return MeshType == AssemblyMeshType;
		}

		bool IsCachableSkeletalMeshPrim(const UE::FUsdPrim& Prim, EMeshCategory Category)
		{
			return 
				MeshTypeIs(ENaniteAssemblyMeshType::SkeletalMesh)
				&& Category == EMeshCategory::Part
				&& Prim.IsInstanceProxy();
		}

		bool GetCachedSkeletalMeshUid(const UE::FUsdPrim& Prim, EMeshCategory Category, FString& OutMeshUid)
		{
			if (IsCachableSkeletalMeshPrim(Prim, Category))
			{
				// Note: By manually fetching the prototype prim for deduplication this will not work with Pregen, where
				// instancing-aware translation is disabled and we're supposed to go through UsdContext.MakeAssetNodeUid()
				// to produce a deduplicated translated node uid. It's probably fine for now as we're still working on
				// skeletal mesh instancing anyway
				UE::FUsdPrim PrototypePrim = UsdUtils::GetPrototypePrim(Prim);
				if (const FString* CachedMeshUid = ImplicitPrototypePrimPathToCachedMeshUid.Find(PrototypePrim.GetPrimPath()))
				{
					OutMeshUid = *CachedMeshUid;
					return true;
				}
			}
			return false;
		}

		void SetCachedSkeletalMeshUid(const UE::FUsdPrim& Prim, EMeshCategory Category, const FString& MeshUid)
		{
			if (IsCachableSkeletalMeshPrim(Prim, Category))
			{
				UE::FUsdPrim PrototypePrim = UsdUtils::GetPrototypePrim(Prim);
				ImplicitPrototypePrimPathToCachedMeshUid.Add(PrototypePrim.GetPrimPath(), MeshUid);
			}
		}

		FString CreateMeshNode(const UE::FSdfPath& Path, EMeshCategory Category, const TArray<FString>& AssemblyDependencies = {})
		{
			FTraversalInfo* Info = TraversalInfoByCachedPrimPath.Find(Path);
			if (!ensure(Info))
			{
				return {};
			}

			const UE::FUsdPrim Prim = AssemblyRootPrim.GetStage().GetPrimAtPath(Path);
			if (!ensure(Prim.IsA(TEXT("Mesh"))))
			{
				return {};
			}

			// If this is a repeated instanced skeletal mesh part, reuse the previously generated uid. This is
			// a workaround for the current lack of instanced skeletal mesh support in Interchange.
			if (FString CachedMeshUid; GetCachedSkeletalMeshUid(Prim, Category, CachedMeshUid))
			{
				return CachedMeshUid;
			}

			// If the mesh node we're creating has assembly dependencies, it must be the first base mesh, and so 
            // attach the traversal result to our traversal info for retrieval during payload creation.
			if (AssemblyDependencies.Num() > 0 && ensure(Result.IsValid()))
			{
				Info->NaniteAssemblyTraversalResult = Result;
			}

			UInterchangeUSDTranslator* Translator = UsdContext.GetTranslator();
			if (!Translator)
			{
				return {};
			}

			TOptional<FHandlerAccumulatedInfo> MeshPrimAccumulatedInfo = Translator->TranslatePrim(Prim, *Info);
			if (!MeshPrimAccumulatedInfo)
			{
				return {};
			}
			AccumulatedInfo.AppendInfo(MeshPrimAccumulatedInfo.GetValue());

			FString MeshUid;
			if (UInterchangeMeshNode* MeshNode = MeshPrimAccumulatedInfo->GetAssetNodeOfClass<UInterchangeMeshNode>())
			{
				MeshUid = MeshNode->GetUniqueID();

				for (const FString& PartUid : AssemblyDependencies)
				{
					MeshNode->AddAssemblyPartDependencyUid(PartUid);
				}

				// Store the mesh uid for the instanced skeletal mesh case
				SetCachedSkeletalMeshUid(Prim, Category, MeshUid);
			}

			if (UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(MeshPrimAccumulatedInfo->GetMainSceneNode()))
			{
				if (UInterchangeBaseNodeContainer* NodeContainer = UsdContext.GetNodeContainer())
				{
					SceneNode->SetCustomLocalTransform(NodeContainer, FTransform::Identity);
				}

				if (!MeshUid.IsEmpty())
				{
					SceneNode->SetCustomAssetInstanceUid(MeshUid);
				}
			}

			return MeshUid;
		}

		UE::FSdfPath GetSkelBindingPrimPathForPart(const UE::FUsdPrim& Prim, const FTraversalInfo& Info)
		{
			// This should only be called for skeletal mesh parts.
			// 
			// Check and get the path to the (possibly ancestor) prim holding skel binding data

			UE::FSdfPath EmptyPath;
			if (!MeshTypeIs(ENaniteAssemblyMeshType::SkeletalMesh))
			{
				return EmptyPath;
			}

			if (Info.ClosestNaniteAssemblySkelBindingPath)
			{
				const UE::FSdfPath SkelBindingPrimPath = UE::FSdfPath(**Info.ClosestNaniteAssemblySkelBindingPath);
				if (SkelBindingPrimPath.IsPrimPath())
				{
					return SkelBindingPrimPath;
				}
			}

			InvalidPartMeshNoSkelBindingPaths.Add(Prim.GetPrimPath());

			return EmptyPath;
		}

		EMeshCategory GetMeshCategory(const UE::FUsdPrim& Prim, const FTraversalInfo& Info, TArray<FPrimPrototypeEntry>& PrototypeStack)
		{
			if (!Prim.IsA(TEXT("Mesh")))
			{
				return EMeshCategory::None;
			}

			// A mesh in a skeletal assembly must:
			//  i: have a bound skeleton 
			// ii: if it's a 'base' mesh (i.e. connected to the designated assembly root skeleton)
			//      - must NOT be inside a pointinstancer
			//      - must NOT be USD native instanced
			//     or, if it's a 'part' mesh (i.e. attached to some other skeleton)
			//      - may be inside a pointinstancer, but, not nested, or
			//      - must be USD native instanced (because we don't want to encourage users to create tons of unique non-instanced parts) 
			//
			if (MeshTypeIs(ENaniteAssemblyMeshType::SkeletalMesh) && Info.BoundSkeletonPrimPath)
			{
				if (BaseSkeletonPrimPath.GetString() == **Info.BoundSkeletonPrimPath) // base skeleton mesh
				{
					if (PrototypeStack.IsEmpty() && !Prim.IsInstanceProxy())
					{
						return EMeshCategory::Base;
					}
				}
				else // part skeleton mesh
				{
					if (PrototypeStack.Num() == 1 || Prim.IsInstanceProxy())
					{
						return EMeshCategory::Part;
					}
				}
			}

			// A part mesh in a static assembly must 
			//  i: not have skel binding data
			// ii: must be inside a pointinstancer prototype, or, be USD native instanced.
			//
			else if (MeshTypeIs(ENaniteAssemblyMeshType::StaticMesh))
			{
				if (!Prim.HasAPI(TEXT("SkelBindingAPI")) && (PrototypeStack.Num() > 0 || Prim.IsInstanceProxy()))
				{
					return EMeshCategory::Part;
				}
			}

			// TODO: communicate all of the above to the user, somehow
			return EMeshCategory::None;
		}

		void HandleOrIgnoreMesh(
			const UE::FUsdPrim& Prim, 
			FTraversalInfo Info, 
			TArray<FTransform>& TransformStack, 
			TArray<FPrimPrototypeEntry>& PrototypeStack)
		{
			if (!Prim.IsA(TEXT("Mesh")) || !Result.IsValid())
			{
				return;
			}

			EMeshCategory Category = GetMeshCategory(Prim, Info, PrototypeStack);
			if (Category == EMeshCategory::None)
			{
				return;
			}

			// If this is a skeletal mesh part, check we have a prim with binding data
			UE::FSdfPath SkelBindingPrimPath;
			if (MeshTypeIs(ENaniteAssemblyMeshType::SkeletalMesh) && Category == EMeshCategory::Part)
			{
				SkelBindingPrimPath = GetSkelBindingPrimPathForPart(Prim, Info);
				if (!SkelBindingPrimPath.IsPrimPath())
				{
					return;
				}
			}

			ensure(
				Result->AddEntry(MakeShared<FMeshEntry>(
					Category, 
					Prim.GetPrimPath(), 
					SkelBindingPrimPath, 
					TransformStack, 
					PrototypeStack)));

			TraversalInfoByCachedPrimPath.Add(Prim.GetPrimPath(), Info);
		}

		void HandleExternalAssetReference(
			const UE::FUsdPrim& Prim,
			FTraversalInfo Info,
			TArray<FTransform>& TransformStack,
			TArray<FPrimPrototypeEntry>& PrototypeStack,
			const FString& AssetReference)
		{
			if (!Result.IsValid())
			{
				return;
			}

			UE::FSdfPath SkelBindingPrimPath;

			// External asset references are only supported on parts.
			EMeshCategory Category = EMeshCategory::Part;

			if (MeshTypeIs(ENaniteAssemblyMeshType::SkeletalMesh))
			{
				// If we found the schema applied to a descendant of the base skelroot prim
				// we should skip as we're not a part.
				if (Prim.GetPrimPath().HasPrefix(BaseSkelRootPrimPath))
				{
					return;
				}

				// This is a skeletal mesh part - check we have prim with binding data
				SkelBindingPrimPath = GetSkelBindingPrimPathForPart(Prim, Info);
				if (!SkelBindingPrimPath.IsPrimPath())
				{
					return;
				}
			}

			TSharedPtr<FMeshEntry> Entry = MakeShared<FMeshEntry>(
				Category,
				Prim.GetPrimPath(),
				SkelBindingPrimPath,
				TransformStack,
				PrototypeStack);

			if (!ensure(Result->AddEntry(Entry)))
			{
				return;
			}

			Entry->NodeUid = AssetReference;

			TraversalInfoByCachedPrimPath.Add(Prim.GetPrimPath(), Info);
		}

		void HandleSkeleton(const UE::FUsdPrim& Prim, FTraversalInfo Info, TArray<FTransform>& TransformStack) const
		{
			if (UInterchangeUSDTranslator* CurrentTranslator = UsdContext.GetTranslator())
			{
				if (TOptional<FHandlerAccumulatedInfo> SkeletonAccumulatedInfo = CurrentTranslator->TranslatePrim(Prim, Info))
				{
					AccumulatedInfo.AppendInfo(SkeletonAccumulatedInfo.GetValue());

					if (UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(SkeletonAccumulatedInfo->GetMainSceneNode()))
					{
						const bool bIsBaseSkeleton = Prim.GetPrimPath() == BaseSkeletonPrimPath;
						const FTransform Transform = bIsBaseSkeleton ? TransformStack.Last() : FTransform::Identity;
						SceneNode->SetCustomLocalTransform(UsdContext.GetNodeContainer(), Transform);
					}
				}
			}
		}

		bool FindAndSetBaseSkeleton()
		{
			if (!MeshTypeIs(ENaniteAssemblyMeshType::SkeletalMesh))
			{
				return true;
			}

			// Get the "base" skeleton prim as specified by the schemas "unreal:naniteAssembly:skeleton" relationship

			const UE::FUsdPrim SkelPrim = UsdToUnreal::NaniteAssemblyUtils::GetBaseSkeletonPrimForSkeletalMeshAssembly(AssemblyRootPrim);
			if (!SkelPrim)
			{
				// Note reason for failure will have been logged by utility call above.
				return false;
			}
			BaseSkeletonPrimPath = SkelPrim.GetPrimPath();

			// We got the skeleton prim, so now check we have an ancestor SkelRoot prim before the assembly root prim in order to try
			// detect weird or broken hierarchy configurations.

			UE::FUsdPrim CurrentPrim = SkelPrim;
			while (!CurrentPrim.IsPseudoRoot())
			{
				if (CurrentPrim.IsA(TEXT("SkelRoot")))
				{
					BaseSkelRootPrimPath = CurrentPrim.GetPrimPath();
					break;
				}
				CurrentPrim = CurrentPrim.GetParent();
			}

			if (BaseSkelRootPrimPath.IsEmpty())
			{
				USD_LOG_USERWARNING(FText::Format(
					LOCTEXT(
						"MissingSkelRoot",
						"Failed to find SkelRoot prim for base Skeleton prim '{0}' while processing NaniteAssemblyRoot prim '{1}'"
					),
					FText::FromString(BaseSkeletonPrimPath.GetString()),
					FText::FromString(AssemblyRootPrim.GetPrimPath().GetString())
				));
				return false;
			}

			return true;
		}

		bool ComputeTraversalResult(FTraversalInfo Info, const FTransform& ParentGlobalTransform)
		{
			Result = MakeShared<FNaniteAssemblyTraversalResult>(AssemblyMeshType, AssemblyRootPrim.GetPrimPath());

			TArray<FTransform> TransformStack = { ParentGlobalTransform };
			TArray<FPrimPrototypeEntry> PrototypeStack;
			if (MeshTypeIs(ENaniteAssemblyMeshType::StaticMesh) && AssemblyRootPrim.IsA(TEXT("PointInstancer")))
			{
				TraversePointInstancerHierarchy(AssemblyRootPrim, Info, TransformStack, PrototypeStack);
			}
			else
			{
				constexpr bool bTraverseInstanceProxies = true;
				for (const UE::FUsdPrim& ChildPrim : AssemblyRootPrim.GetFilteredChildren(bTraverseInstanceProxies))
				{
					TraverseImpl(ChildPrim, Info, TransformStack, PrototypeStack);
				}
			}

			if (!ValidateTraversalResult())
			{
				Result.Reset();
				return false;
			}

			return true;
		}

		void TraversePointInstancerHierarchy(
			const UE::FUsdPrim& Prim,
			FTraversalInfo Info,
			TArray<FTransform> TransformStack,
			TArray<FPrimPrototypeEntry>& PrototypeStack
		)
		{
			const TArray<UE::FSdfPath> PrototypePaths = UsdToUnreal::NaniteAssemblyUtils::GetPointInstancerPrototypePaths(Prim);
			if (PrototypePaths.IsEmpty())
			{
				return;
			}

			for (int32 PrototypeIndex = 0; PrototypeIndex < PrototypePaths.Num(); ++PrototypeIndex)
			{
				UE::FUsdPrim PrototypePrim = Prim.GetStage().GetPrimAtPath(PrototypePaths[PrototypeIndex]);
				if (!PrototypePrim)
				{
					continue;
				}

				FTraversalInfo PrototypePrimInfo = Info;
				PrototypePrimInfo.UpdateWithCurrentPrim(PrototypePrim);

				// Push transforms and prototype before entering each pointinstancer prototype
				TransformStack.Add(FTransform::Identity);
				PrototypeStack.Emplace(Prim.GetPrimPath(), PrototypePrim.GetPrimPath(), PrototypeIndex, PrototypePaths.Num());

				TraverseImpl(PrototypePrim, PrototypePrimInfo, TransformStack, PrototypeStack);

				// Pop before entering the next
				PrototypeStack.Pop();
				TransformStack.Pop();
			}
		}

		void TraverseImpl(const UE::FUsdPrim& Prim, FTraversalInfo Info, TArray<FTransform> TransformStack, TArray<FPrimPrototypeEntry>& PrototypeStack)
		{
			if (!ensure(TransformStack.Num() > 0))
			{
				return;
			}

			if (Prim.HasAPI(TEXT("NaniteAssemblyRootAPI")))
			{
				USD_LOG_USERWARNING(FText::Format(
					LOCTEXT("NestedSchemas", "Skipping nested NaniteAssemblyRootAPI prim '{0}' found below current root '{1}'"),
					FText::FromString(Prim.GetPrimPath().GetString()),
					FText::FromString(AssemblyRootPrim.GetPrimPath().GetString())
				));
				return;
			}

			if (Prim.IsA(TEXT("Material")))
			{
				return;
			}

			Info.UpdateWithCurrentPrim(Prim);

			if (Prim.IsA(TEXT("Xformable")))
			{
				FTransform LocalTransform = FTransform::Identity;
				bool bResetTransformStack = false;
				UsdToUnreal::ConvertXformable(Prim.GetStage(), UE::FUsdTyped(Prim), LocalTransform, UsdUtils::GetEarliestTimeCode(), &bResetTransformStack);

				FTransform& CurrentTransform = TransformStack.Last();
				CurrentTransform = LocalTransform * CurrentTransform;
			}

			if (FString AssetRef; UsdToUnreal::NaniteAssemblyUtils::GetExternalAssetReference(Prim, AssetRef))
			{
				HandleExternalAssetReference(Prim, Info, TransformStack, PrototypeStack, AssetRef);
				// The user is mostly likely expecting the subtree below the prim with the external
				// reference applied to be pruned, so we stop the traversal.
				return;
			}

			if (Prim.IsA(TEXT("Mesh")))
			{
				HandleOrIgnoreMesh(Prim, Info, TransformStack, PrototypeStack);
			}
			else if (Prim.IsA(TEXT("PointInstancer")))
			{
				TraversePointInstancerHierarchy(Prim, Info, TransformStack, PrototypeStack);
			}
			else if (Prim.IsA(TEXT("Skeleton")))
			{
				HandleSkeleton(Prim, Info, TransformStack);
			}
			else
			{
				constexpr bool bTraverseInstanceProxies = true;
				for (const UE::FUsdPrim& ChildPrim : Prim.GetFilteredChildren(bTraverseInstanceProxies))
				{
					TraverseImpl(ChildPrim, Info, TransformStack, PrototypeStack);
				}
			}
		}

		bool ValidateHierarchy(const FTraversalInfo& Info) const
		{
			if (AssemblyMeshType == ENaniteAssemblyMeshType::None)
			{
				return false;
			}

			// Nanite assemblies cannot be inside LODs.
			if (Info.bInsideLODVariant || Info.bIsLODVariantContainer)
			{
				USD_LOG_USERWARNING(FText::Format(
					LOCTEXT("InsideLodVariant", "Ignoring NaniteAssemblyRootAPI prim '{0}' which is inside an LOD (not supported)."),
					FText::FromString(AssemblyRootPrim.GetPrimPath().GetString())
				));
				return false;
			}

			// Check ancestors for configurations that we know will cause problems:

			UE::FUsdPrim CurrentPrim = AssemblyRootPrim;

			while (!CurrentPrim.IsPseudoRoot())
			{
				if (CurrentPrim != AssemblyRootPrim && CurrentPrim.IsA(TEXT("PointInstancer")))
				{
					USD_LOG_USERWARNING(FText::Format(
						LOCTEXT("ChildOfPointInstancer", "NaniteAssemblyRoot '{0}' cannot currently be a descendant of a pointinstancer - '{1}'"),
						FText::FromString(AssemblyRootPrim.GetPrimPath().GetString()),
						FText::FromString(CurrentPrim.GetPrimPath().GetString())
					));
					return false;
				}
				else if (CurrentPrim.HasAPI(*UsdToUnreal::ConvertToken(UnrealIdentifiers::LodSubtreeAPI)))
				{
					USD_LOG_USERWARNING(FText::Format(
						LOCTEXT("ChildOfLodSubtree", "NaniteAssemblyRoot '%s' cannot be nested inside an {0} container."),
						FText::FromString(AssemblyRootPrim.GetPrimPath().GetString()),
						FText::FromString(UsdToUnreal::ConvertToken(UnrealIdentifiers::LodSubtreeAPI))
					));
					return false;
				}
				CurrentPrim = CurrentPrim.GetParent();
			}

			return true;
		}

		bool ValidateTraversalResult() const
		{
			if (!Result)
			{
				return false;
			}

			// Report skel mesh parts that were dropped due to missing bindings
			if (InvalidPartMeshNoSkelBindingPaths.Num() > 0)
			{
				FString Suffix;
				for (int32 Index = 0; Index < InvalidPartMeshNoSkelBindingPaths.Num(); ++Index)
				{
					// there might be a massive number of prims here, so only print the first few
					int32 constexpr MaxEntries = 50;
					if (Index < MaxEntries)
					{
						Suffix.Append(FString::Printf(TEXT("... %s\n"), *InvalidPartMeshNoSkelBindingPaths[Index].GetString()));
					}
					else
					{
						Suffix.Append(FText::Format(LOCTEXT("SkipSubstring", "... (skipped {0} more)"), InvalidPartMeshNoSkelBindingPaths.Num() - MaxEntries).ToString());
						break;
					}
				}

				USD_LOG_USERWARNING(FText::Format(
					LOCTEXT(
						"SkeletalPrimsMissingJointData",
						"Ignoring ({0}) skeletal mesh prims missing joint binding data while generating parts for Nanite assembly prim '{1}'.\nPlease check that the {2} schema is applied to an ancestor prim and valid primvar data authored for attributes '{3}' and '{4}'...\n{5}"
					),
					InvalidPartMeshNoSkelBindingPaths.Num(),
					FText::FromString(AssemblyRootPrim.GetPrimPath().GetString()),
					FText::FromString(UsdToUnreal::ConvertToken(UnrealIdentifiers::NaniteAssemblySkelBindingAPI)),
					FText::FromString(UsdToUnreal::ConvertToken(UnrealIdentifiers::PrimvarsUnrealNaniteAssemblyBindJoints)),
					FText::FromString(UsdToUnreal::ConvertToken(UnrealIdentifiers::PrimvarsUnrealNaniteAssemblyBindJointWeights)),
					FText::FromString(Suffix)
				));
			}

			// Check we found some parts, and, if this is a skeletal mesh assembly, we also require at least one base mesh
			const bool bHasBaseMeshes = Result->HasEntriesForCategory(EMeshCategory::Base);
			const bool bHasPartMeshes = Result->HasEntriesForCategory(EMeshCategory::Part);
			if (!bHasPartMeshes || (MeshTypeIs(ENaniteAssemblyMeshType::SkeletalMesh) && !bHasBaseMeshes))
			{
				USD_LOG_USERWARNING(FText::Format(
					LOCTEXT("MissingMeshes", "Failed to find any valid base/part meshes for NaniteAssemblyRootAPI prim '{0}'"),
					FText::FromString(AssemblyRootPrim.GetPrimPath().GetString())
				));

				return false;
			}

			return true;
		}

	public:

		void Traverse(const UE::FUsdPrim& InAssemblyRootPrim, FTraversalInfo Info, const FTransform& ParentGlobalTransform)
		{
			AssemblyRootPrim = InAssemblyRootPrim;

			if (!AssemblyRootPrim.HasAPI(TEXT("NaniteAssemblyRootAPI")))
			{
				return;
			}

			// Setup and initial validation

			AssemblyMeshType = UsdToUnreal::NaniteAssemblyUtils::GetNaniteAssemblyMeshType(AssemblyRootPrim);

			if (!ValidateHierarchy(Info) || !FindAndSetBaseSkeleton())
			{
				return;
			}

			// Compute the traversal result that will be attached to the traversal info object of
			// the skeletal or static assembly base mesh. 
			if (!ComputeTraversalResult(Info, ParentGlobalTransform) || !Result.IsValid())
			{
				return;
			}

			// Now create mesh and scene nodes for parts. Do these first because we will need the resulting node 
			// uids when creating the base meshes in a moment, in order to setup the correct dependencies.
			for (TSharedPtr<FMeshEntry>& Entry : Result->GetEntriesForCategory(EMeshCategory::Part))
			{
				// Note that external asset ref part entries will have already set the uid to whatever asset.
				if (Entry->NodeUid.IsEmpty()) 
				{
					// If create mesh fails here we'll wind up with an invalid/empty node uid for the entry.
					// That's ok however because the payload processing will validate all entries anyway.
					Entry->NodeUid = CreateMeshNode(Entry->PrimPath, Entry->Category);
				}
			}

			// Skeletal mesh - now create base meshes and register dependencies on the parts we created
			// above. Note also that we only need to apply the dependencies to one of the base meshes
			// since Interchange will currently combine all the meshes anyway.
			if (MeshTypeIs(ENaniteAssemblyMeshType::SkeletalMesh))
			{
				static const TArray<FString> EmptyDependencyArray;
				bool bIsFirst = true;
				for (TSharedPtr<FMeshEntry>& Entry : Result->GetEntriesForCategory(EMeshCategory::Base))
				{
					const TArray<FString> PartDependencies = bIsFirst 
						? Result->GetMeshUidsForCategory(EMeshCategory::Part) 
						: EmptyDependencyArray;

					Entry->NodeUid = CreateMeshNode(Entry->PrimPath, Entry->Category, PartDependencies);

					bIsFirst = false;
				}
			}
			// Static mesh - the NaniteAssemblyRootAPI schema doesn't currently support specifying a base 
			// mesh when in static mesh mode. So here we create a dummy mesh node that will result in an 
			// empty mesh that the mesh factory can eventually add the assembly data to. 
			else if (MeshTypeIs(ENaniteAssemblyMeshType::StaticMesh))
			{
				const FString MeshUid = UsdContext.MakeAssetNodeUid(AssemblyRootPrim, MeshPrefix);
				const FString NodeName(AssemblyRootPrim.GetPrimPath().GetName());

				UInterchangeBaseNodeContainer* NodeContainer = UsdContext.GetNodeContainer();
				if (!NodeContainer)
				{
					return;
				}

				UInterchangeMeshNode* MeshNode = NewObject<UInterchangeMeshNode>(NodeContainer);
				NodeContainer->SetupNode(MeshNode, MeshUid, NodeName, EInterchangeNodeContainerType::TranslatedAsset);
				MeshNode->SetAssetName(NodeName);
				MeshNode->SetPayLoadKey(AssemblyRootPrim.GetPrimPath().GetString(), EInterchangeMeshPayLoadType::STATIC);
				AccumulatedInfo.PrimAssetNodes.Add(MeshNode);
				
				UE::Interchange::USD::SetPrimPath(*MeshNode, AssemblyRootPrim.GetPrimPath().GetString());

				for (const FString& PartUid : Result->GetMeshUidsForCategory(EMeshCategory::Part))
				{
					MeshNode->AddAssemblyPartDependencyUid(PartUid);
				}

				ParentSceneNode->SetCustomAssetInstanceUid(MeshUid);

				// Store the traversal result and cache the info for the mesh
				Info.NaniteAssemblyTraversalResult = Result;
				FWriteScopeLock ScopedInfoWriteLock{ UsdContext.NodeUidToCachedTraversalInfoLock };
				UsdContext.NodeUidToCachedTraversalInfo.Add(MeshUid, Info);
			}
		}
	};
}	 // namespace UE::NaniteAssemblySchemaHandler::Private

namespace UE::Interchange::USD
{
	const FString& FNaniteAssemblySchemaHandler::GetHandlerName() const
	{
		const static FString HandlerName = TEXT("NaniteAssemblyHandler");
		return HandlerName;
	}

	const FString& FNaniteAssemblySchemaHandler::GetTargetSchemaName() const
	{
		const static FString SchemaName = TEXT("Xformable");
		return SchemaName;
	}

	bool FNaniteAssemblySchemaHandler::CanHandlePrim(const UE::FUsdPrim& Prim, const UInterchangeUsdContext& UsdContext) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FNaniteAssemblySchemaHandler::CanHandlePrim)

		return Prim.HasAPI(TEXT("NaniteAssemblyRootAPI"));
	}

	TOptional<bool> FNaniteAssemblySchemaHandler::CanBeCollapsed(const UE::FUsdPrim& Prim, UInterchangeUsdContext& UsdContext) const
	{
		// We may have some more interactions between Nanite Assemblies and collapsing in the future, but for now let's just
		// stop collapsing if we run into one
		return false;
	}
	
	TOptional<bool> FNaniteAssemblySchemaHandler::CollapsesChildren(const UE::FUsdPrim& Prim, UInterchangeUsdContext& UsdContext) const
	{
		// We may have some more interactions between Nanite Assemblies and collapsing in the future, but for now let's just
		// stop collapsing if we run into one
		return false;
	}

	bool FNaniteAssemblySchemaHandler::OnTranslate(
		const UE::FUsdPrim& Prim,
		FTraversalInfo& TraversalInfo,
		FHandlerAccumulatedInfo& AccumulatedInfo,
		UInterchangeUsdContext& UsdContext
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FNaniteAssemblySchemaHandler::OnTranslate)

		using namespace UE::NaniteAssemblySchemaHandler::Private;

		UInterchangeSceneNode* ParentSceneNode = Cast<UInterchangeSceneNode>(AccumulatedInfo.GetOrCreateMainSceneNode(Prim, TraversalInfo, UsdContext));
		if (ParentSceneNode)
		{
			const FTransform ParentGlobalTransform;
			FNaniteAssemblyTraversalHelper NaniteAssemblyTraversalHelper(UsdContext, AccumulatedInfo, ParentSceneNode);
			NaniteAssemblyTraversalHelper.Traverse(Prim, TraversalInfo, ParentGlobalTransform);
		}

		// This schema handler takes charge of recursive traversal, so let's prevent the UsdTranslator from visiting any children again.
		// We'll already get this done automatically for some prims as we call TranslatePrim() directly for e.g. Meshes, but we really
		// don't want the USD translator visiting any other of our children either
		constexpr bool bTraverseInstanceProxies = true;
		for (const FUsdPrim& ChildPrim : Prim.GetFilteredChildren(bTraverseInstanceProxies))
		{
			UsdContext.HandledPrimInfo.FindOrAdd(ChildPrim.GetPrimPath());
		}

		return true;
	}
}	 // namespace UE::Interchange::USD

#undef LOCTEXT_NAMESPACE

#endif	  // USE_USD_SDK
