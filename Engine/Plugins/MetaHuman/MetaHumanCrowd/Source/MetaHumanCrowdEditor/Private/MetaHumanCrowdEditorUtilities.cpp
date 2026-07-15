// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCrowdEditorUtilities.h"

#include "MetaHumanCrowdEditorLog.h"
#include "MetaHumanCrowdTypes.h"
#include "Commands/DNACalibSetLODsCommand.h"
#include "DNAAssetUserData.h"
#include "DNACalibDNAReader.h"
#include "DNA.h"
#include "SkelMeshDNAUtils.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimBoneCompressionSettings.h"
#include "AnimationBlueprintLibrary.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "PreviewScene.h"
#include "BoneWeights.h"
#include "Logging/StructuredLog.h"
#include "MaterialShared.h"
#include "MeshBoneReduction.h"
#include "MeshDescription.h"
#include "Misc/Guid.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "NaniteSceneProxy.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Serialization/MemoryWriter.h"
#include "SkeletalMeshAttributes.h"
#include "SkeletalMeshOperations.h"
#include "SkeletalMeshTypes.h"
#include "StaticMeshAttributes.h"

#include "Commands/DNACalibRemoveAnimatedMapCommand.h"

namespace UE::MetaHuman::CrowdEditorUtilities
{

static void ConfigureBakedAnimSequence(
	TNotNull<UAnimSequence*> InAnim,
	TNotNull<USkeleton*> InSkeleton,
	TObjectPtr<UAnimBoneCompressionSettings> BoneCompressionSettingsOverride)
{
	InAnim->SetSkeleton(InSkeleton);
	InAnim->SetPreviewMesh(InSkeleton->GetAssetPreviewMesh(InAnim));
	InAnim->AssetImportData = nullptr;
	InAnim->BoneCompressionSettings = BoneCompressionSettingsOverride;
}

void SafeEnableNanite(TNotNull<USkeletalMesh*> Mesh)
{
	UMaterialInterface* UnsupportedMaterialReplacement = nullptr;

	if (Mesh->NaniteSettings.bEnabled)
	{
		return;
	}

	UE::MetaHuman::CrowdEditorUtilities::FScopedSkeletalMeshChange ScopedSkeletalMeshChange(Mesh);

	for (FSkeletalMaterial& SkelMaterial : Mesh->GetMaterials())
	{
		if (!SkelMaterial.MaterialInterface)
		{
			continue;
		}

		if (!Nanite::IsSupportedBlendMode(*SkelMaterial.MaterialInterface))
		{
			if (!UnsupportedMaterialReplacement)
			{
				UnsupportedMaterialReplacement = LoadObject<UMaterialInterface>(nullptr, TEXTVIEW("/" UE_PLUGIN_NAME "/M_MaskedTransparent.M_MaskedTransparent"));
			}

			// If UnsupportedMaterialReplacement can't be loaded, this will be set to nullptr and 
			// should use the engine default material.
			SkelMaterial.MaterialInterface = UnsupportedMaterialReplacement;
		}

		if (SkelMaterial.MaterialInterface)
		{
			// Only enter this scope if the flags need setting, because FMaterialUpdateContext is a
			// heavy operation.
			if (!SkelMaterial.MaterialInterface->GetUsageByFlag(MATUSAGE_SkeletalMesh)
				|| !SkelMaterial.MaterialInterface->GetUsageByFlag(MATUSAGE_Nanite)
				|| !SkelMaterial.MaterialInterface->GetUsageByFlag(MATUSAGE_InstancedSkinnedMesh))
			{
				FMaterialUpdateContext UpdateContext;
				// Ensure the material has the usage flags required for Nanite skeletal mesh rendering.
				// Without these, NaniteResources.cpp will replace the material with the engine default at runtime.
				SkelMaterial.MaterialInterface->SetMaterialUsage(MATUSAGE_SkeletalMesh);
				SkelMaterial.MaterialInterface->SetMaterialUsage(MATUSAGE_Nanite);
				SkelMaterial.MaterialInterface->SetMaterialUsage(MATUSAGE_InstancedSkinnedMesh);
			}
		}
	}

	Mesh->NaniteSettings.bEnabled = true;
}

void ApplyMinLODToMesh(USkeletalMesh* Mesh, const FPerPlatformInt& MinLOD, const FPerQualityLevelInt& QualityLevelMinLOD)
{
	if (!Mesh)
	{
		return;
	}

	const int32 NumLODs = Mesh->GetLODNum();
	if (NumLODs <= 0)
	{
		return;
	}

	const int32 MaxValidLOD = NumLODs - 1;

	FPerPlatformInt ClampedMinLOD;
	ClampedMinLOD.Default = FMath::Clamp(MinLOD.Default, 0, MaxValidLOD);
	ClampedMinLOD.PerPlatform.Reserve(MinLOD.PerPlatform.Num());
	for (const TPair<FName, int32>& Pair : MinLOD.PerPlatform)
	{
		ClampedMinLOD.PerPlatform.Add(Pair.Key, FMath::Clamp(Pair.Value, 0, MaxValidLOD));
	}

	FPerQualityLevelInt ClampedQualityLevelMinLOD;
	ClampedQualityLevelMinLOD.Default = FMath::Clamp(QualityLevelMinLOD.Default, 0, MaxValidLOD);
	ClampedQualityLevelMinLOD.PerQuality.Reserve(QualityLevelMinLOD.PerQuality.Num());
	for (const TPair<int32, int32>& Pair : QualityLevelMinLOD.PerQuality)
	{
		ClampedQualityLevelMinLOD.PerQuality.Add(Pair.Key, FMath::Clamp(Pair.Value, 0, MaxValidLOD));
	}

	Mesh->SetMinLod(MoveTemp(ClampedMinLOD));
	Mesh->SetQualityLevelMinLod(MoveTemp(ClampedQualityLevelMinLOD));
}

static void EnableRecomputeTangents(USkeletalMesh* Mesh, int32 LODIndexThreshold, TConstArrayView<FName> MaterialSlotNames, ESkinVertexColorChannel VertexMaskChannel)
{
	if (!Mesh || LODIndexThreshold < 0 || MaterialSlotNames.IsEmpty())
	{
		return;
	}

	FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel();
	if (!ImportedModel)
	{
		return;
	}

	const int32 NumLODs = Mesh->GetLODNum();
	if (NumLODs <= 0)
	{
		return;
	}

	const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
	const int32 LastLODIndex = FMath::Min(LODIndexThreshold, NumLODs - 1);

	for (int32 LODIndex = 0; LODIndex <= LastLODIndex; ++LODIndex)
	{
		if (!ImportedModel->LODModels.IsValidIndex(LODIndex))
		{
			continue;
		}

		FSkeletalMeshLODInfo* LODInfo = Mesh->GetLODInfo(LODIndex);
		if (!LODInfo)
		{
			continue;
		}

		FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[LODIndex];

		// Enable recompute tangents on every section whose material slot name appears in the
		// caller-supplied list
		bool bAnyMatched = false;
		for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); ++SectionIndex)
		{
			FSkelMeshSection& Section = LODModel.Sections[SectionIndex];
			if (!Materials.IsValidIndex(Section.MaterialIndex))
			{
				continue;
			}

			if (!MaterialSlotNames.Contains(Materials[Section.MaterialIndex].MaterialSlotName))
			{
				continue;
			}

			bAnyMatched = true;

			Section.bRecomputeTangent = true;
			Section.RecomputeTangentsVertexMaskChannel = VertexMaskChannel;

			// Mirror the changes into UserSectionsData so they survive a build.
			FSkelMeshSourceSectionUserData& SourceSectionUserData = LODModel.UserSectionsData.FindOrAdd(Section.OriginalDataSectionIndex);
			SourceSectionUserData.bDisabled = Section.bDisabled;
			SourceSectionUserData.bCastShadow = Section.bCastShadow;
			SourceSectionUserData.bVisibleInRayTracing = Section.bVisibleInRayTracing;
			SourceSectionUserData.bRecomputeTangent = Section.bRecomputeTangent;
			SourceSectionUserData.RecomputeTangentsVertexMaskChannel = Section.RecomputeTangentsVertexMaskChannel;
			SourceSectionUserData.GenerateUpToLodIndex = Section.GenerateUpToLodIndex;
			SourceSectionUserData.CorrespondClothAssetIndex = Section.CorrespondClothAssetIndex;
			SourceSectionUserData.ClothingData = Section.ClothingData;
		}

		// Recompute tangents requires the skin cache
		if (bAnyMatched)
		{
			LODInfo->SkinCacheUsage = ESkinCacheUsage::Enabled;
		}
	}
}

static void ApplyLODScreenSizes(USkeletalMesh* Mesh, TConstArrayView<FPerPlatformFloat> LODScreenSizes, float ScaleFactor)
{
	if (!Mesh || LODScreenSizes.IsEmpty() || ScaleFactor <= 0.0f)
	{
		return;
	}

	const int32 NumLODs = Mesh->GetLODNum();
	if (NumLODs <= 0)
	{
		return;
	}

	UE::MetaHuman::CrowdEditorUtilities::FScopedSkeletalMeshChange ScopedSkeletalMeshChange(Mesh);

	const int32 NumToApply = FMath::Min(LODScreenSizes.Num(), NumLODs);

	for (int32 LODIndex = 0; LODIndex < NumToApply; ++LODIndex)
	{
		FSkeletalMeshLODInfo* LODInfo = Mesh->GetLODInfo(LODIndex);
		if (!LODInfo)
		{
			continue;
		}

		// Scale Default and every per-platform override by ScaleFactor
		FPerPlatformFloat Scaled = LODScreenSizes[LODIndex];
		Scaled.Default *= ScaleFactor;
		for (TPair<FName, float>& Pair : Scaled.PerPlatform)
		{
			Pair.Value *= ScaleFactor;
		}

		LODInfo->ScreenSize = MoveTemp(Scaled);
	}
}

void RemoveUnusedBones(TNotNull<USkeletalMesh*> Mesh)
{
	UE::MetaHuman::CrowdEditorUtilities::FScopedSkeletalMeshChange ScopedSkeletalMeshChange(Mesh);

	IMeshBoneReductionModule& MeshBoneReductionModule = FModuleManager::Get().LoadModuleChecked<IMeshBoneReductionModule>("MeshBoneReduction");
	IMeshBoneReduction& MeshBoneReduction = *MeshBoneReductionModule.GetMeshBoneReductionInterface();
	
	const TArray<FName> ForceKeepBones;
	TArray<FName> BonesToRemove;
	MeshBoneReduction.BuildBonesToBeRemovedUsedBySkinWeights(Mesh, ForceKeepBones, BonesToRemove);

	MeshBoneReduction.ReduceBoneCounts(Mesh, ForceKeepBones, BonesToRemove, /* LODIndex */ 0, /* bIncludeBelowLODs */ true);
}

void SetOptimizeForInstancing(TNotNull<USkeletalMesh*> Mesh)
{
	for (int32 LODIndex = 0; LODIndex < Mesh->GetLODNum(); LODIndex++)
	{
		FSkeletalMeshLODInfo* LODInfo = Mesh->GetLODInfo(LODIndex);
		LODInfo->BuildSettings.bOptimizeForInstancing = true;
	}
}

bool TryRebindToSkeleton(TNotNull<USkeletalMesh*> Mesh, TNotNull<USkeleton*> NewSkeleton)
{
	const FReferenceSkeleton& MeshRefSkel = Mesh->GetRefSkeleton();
	const FReferenceSkeleton& TargetRefSkel = NewSkeleton->GetReferenceSkeleton();

	// Validate that the target skeleton shares at least one bone with the mesh.
	bool bHasCommonBone = false;
	for (int32 BoneIndex = 0; BoneIndex < MeshRefSkel.GetRawBoneNum(); ++BoneIndex)
	{
		if (TargetRefSkel.FindRawBoneIndex(MeshRefSkel.GetBoneName(BoneIndex)) != INDEX_NONE)
		{
			bHasCommonBone = true;
			break;
		}
	}

	if (!bHasCommonBone)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "TryRebindToSkeleton: Skeleton '{Skeleton}' has no bones in common with mesh '{Mesh}'",
			("Skeleton", *NewSkeleton->GetPathName()), ("Mesh", *Mesh->GetPathName()));
		return false;
	}

	// Build the list of bone names to remove: bones present in the mesh but absent from the target skeleton.
	TArray<FName> BoneNamesToRemove;
	for (int32 BoneIndex = 0; BoneIndex < MeshRefSkel.GetRawBoneNum(); ++BoneIndex)
	{
		const FName BoneName = MeshRefSkel.GetBoneName(BoneIndex);
		if (TargetRefSkel.FindRawBoneIndex(BoneName) == INDEX_NONE)
		{
			BoneNamesToRemove.Add(BoneName);
		}
	}

	// If MeshDescriptions are not present, reconstruct them from the ImportedModel
	// so that the bone cleanup below has data to work with.
	FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel();
	if (ImportedModel)
	{
		const int32 NumImportedLODs = ImportedModel->LODModels.Num();

		if (NumImportedLODs > 0 && ImportedModel->LODModels[0].NumVertices == 0)
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "TryRebindToSkeleton: LOD 0 of mesh '{Mesh}' has no vertices",
				("Mesh", *Mesh->GetPathName()));
			return false;
		}

		if (Mesh->GetNumSourceModels() < NumImportedLODs)
		{
			Mesh->SetNumSourceModels(NumImportedLODs);
		}

		for (int32 LODIndex = 0; LODIndex < NumImportedLODs; ++LODIndex)
		{
			if (!Mesh->HasMeshDescription(LODIndex))
			{
				FMeshDescription MeshDescription;
				ImportedModel->LODModels[LODIndex].GetMeshDescription(Mesh, LODIndex, MeshDescription);
				Mesh->CreateMeshDescription(LODIndex, MoveTemp(MeshDescription));
				Mesh->CommitMeshDescription(LODIndex);
			}
		}
	}

	// Clean MeshDescriptions: redistribute skin weights away from removed bones, then delete the
	// bone elements entirely. We capture the surviving bone set from LOD 0 after cleanup so that
	// the RefSkeleton we build matches the compacted MeshDescription bone indices exactly.
	struct FSurvivingBone
	{
		FName Name;
		int32 ParentIndex = INDEX_NONE;
	};
	TArray<FSurvivingBone> SurvivingBones;

	const int32 NumLODs = Mesh->GetLODNum();
	for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
	{
		const FMeshDescription* ExistingDesc = Mesh->GetMeshDescription(LODIndex);
		if (!ExistingDesc)
		{
			continue;
		}

		FMeshDescription MeshDesc = *ExistingDesc;

		if (BoneNamesToRemove.Num() > 0)
		{
			UE::AnimationCore::FBoneWeightsSettings BoneWeightSettings;
			FSkeletalMeshOperations::RemoveBones(MeshDesc, BoneNamesToRemove, BoneWeightSettings);
		}

		FSkeletalMeshOperations::RemoveUnusedBones(MeshDesc);

		// Capture the surviving bone set from the cleaned LOD 0 MeshDescription. After
		// RemoveUnusedBones, bone element IDs are compacted and contiguous, and parent indices
		// are already remapped to the compacted space. We use this to build a RefSkeleton whose
		// indices are guaranteed to match the MeshDescription bone indices.
		if (LODIndex == 0)
		{
			FSkeletalMeshConstAttributes Attrs(MeshDesc);
			check(Attrs.HasBones());

			FSkeletalMeshAttributesShared::FBoneNameAttributesConstRef BoneNameAttr = Attrs.GetBoneNames();
			FSkeletalMeshAttributes::FBoneParentIndexAttributesConstRef BoneParentAttr = Attrs.GetBoneParentIndices();

			for (const FBoneID BoneID : Attrs.Bones().GetElementIDs())
			{
				FSurvivingBone& Bone = SurvivingBones.AddDefaulted_GetRef();
				Bone.Name = BoneNameAttr.Get(BoneID);
				Bone.ParentIndex = BoneParentAttr.Get(BoneID);
			}
		}

		Mesh->CreateMeshDescription(LODIndex, MoveTemp(MeshDesc));
		Mesh->CommitMeshDescription(LODIndex);
	}

	// LOD 0 should always have a valid Mesh Description and therefore SurvivingBones should be 
	// populated correctly.
	ensure(!SurvivingBones.IsEmpty());

	// Rebuild the RefSkeleton from the bones that survived in the cleaned LOD 0 MeshDescription,
	// preserving the mesh's own rest poses. The bone set and ordering comes from the compacted
	// MeshDescription (which RemoveUnusedBones pruned to only skin-weighted bones and their
	// ancestors), ensuring the RefSkeleton indices match the MeshDescription bone indices.
	FReferenceSkeleton NewRefSkel;
	{
		FReferenceSkeletonModifier Modifier(NewRefSkel, NewSkeleton);
		for (const FSurvivingBone& Bone : SurvivingBones)
		{
			const int32 OrigIdx = MeshRefSkel.FindRawBoneIndex(Bone.Name);
			check(OrigIdx != INDEX_NONE);

			FMeshBoneInfo BoneInfo;
			BoneInfo.Name = Bone.Name;
			BoneInfo.ParentIndex = Bone.ParentIndex;

			const FTransform& BonePose = MeshRefSkel.GetRawRefBonePose()[OrigIdx];
			Modifier.Add(BoneInfo, BonePose);
		}
	}

	// Apply the new skeleton, cleaned ref skeleton, and trigger a full rebuild.
	{
		UE::MetaHuman::CrowdEditorUtilities::FScopedSkeletalMeshChange ScopedSkeletalMeshChange(Mesh);

		Mesh->SetSkeleton(NewSkeleton);
		Mesh->SetRefSkeleton(NewRefSkel);
		Mesh->CalculateInvRefMatrices();
	}

	return true;
}

UAnimSequence* BakeAnimation(
	TNotNull<UAnimSequence*> SourceAnim,
	TNotNull<USkeletalMesh*> TargetMesh,
	TObjectPtr<UAnimBoneCompressionSettings> BoneCompressionSettingsOverride,
	const FString& AnimAssetName,
	UObject* Outer,
	FName FaceRootBoneName)
{
	const int32 NumFrames = SourceAnim->GetNumberOfSampledKeys();

	if (NumFrames <= 0)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "BakeAnimation: source animation '{AnimName}' has no sampled keys.", SourceAnim->GetName());
		return nullptr;
	}

	const FFrameRate FrameRate = SourceAnim->GetSamplingFrameRate();

	if (!FrameRate.IsValid() || FrameRate.AsDecimal() <= 0.0)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "BakeAnimation: source animation '{AnimName}' has an invalid frame rate.", SourceAnim->GetName());
		return nullptr;
	}

	USkeleton* TargetSkeleton = TargetMesh->GetSkeleton();
	if (!TargetSkeleton)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "BakeAnimation: target mesh '{Mesh}' has no skeleton.", TargetMesh->GetName());
		return nullptr;
	}

	const FReferenceSkeleton& TargetRefSkel = TargetSkeleton->GetReferenceSkeleton();

	// Resolve which target bones are inside the face sub-tree. Strict descendants of
	// FaceRootBoneName end up in the bake; the root itself (e.g. 'head') is intentionally
	// excluded so the body animation's head transform passes through unchanged. The face
	// bake on the head mesh would otherwise overwrite head with the post-process ABP's view
	// of it, losing any body-driven head motion.
	TSet<int32> FaceSubtreeBoneIndices;
	if (!FaceRootBoneName.IsNone())
	{
		const int32 FaceRootTargetIdx = TargetRefSkel.FindBoneIndex(FaceRootBoneName);
		if (FaceRootTargetIdx != INDEX_NONE)
		{
			const int32 NumTargetBones = TargetRefSkel.GetNum();
			for (int32 BoneIdx = 0; BoneIdx < NumTargetBones; ++BoneIdx)
			{
				if (TargetRefSkel.BoneIsChildOf(BoneIdx, FaceRootTargetIdx))
				{
					FaceSubtreeBoneIndices.Add(BoneIdx);
				}
			}
		}
		else
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning,
				"BakeAnimation: FaceRootBoneName '{BoneName}' not found on target skeleton '{Skeleton}'; no bones will "
				"be baked from the head pose, all tracks will pass through from the source animation.",
				FaceRootBoneName, TargetSkeleton->GetName());
		}
	}

	// If the caller specified a FaceRootBoneName, only bones inside the resolved sub-tree get
	// baked from the head mesh pose -- never the face root itself, never anything outside it.
	// If the caller passed NAME_None, the filter is disabled and every bone the head mesh has
	// skin weights for is baked.
	auto IsInFaceSubtree = [&FaceSubtreeBoneIndices, &FaceRootBoneName](int32 TargetBoneIdx) -> bool
	{
		return FaceRootBoneName.IsNone() || FaceSubtreeBoneIndices.Contains(TargetBoneIdx);
	};

	// Create a FPreviewScene to host the component. The component is not world-ticked,
	// instead we drive it manually per frame.
	FPreviewScene PreviewScene;

	USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	Component->SetSkeletalMesh(TargetMesh);
	Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Component->SetAnimation(SourceAnim);

	Component->bEnableUpdateRateOptimizations = false;
	Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	// Allow ctrl_expressions_* curves through the BoneContainer filter so RigLogic in the
	// PostProcess ABP receives them and can drive FACIAL_* bones. Without this, the curves get
	// stripped during InitAnim and RigLogic produces ref-pose output (eyes don't blink, etc.).
	TArray<FName> FloatCurveNames;
	UAnimationBlueprintLibrary::GetAnimationCurveNames(SourceAnim, ERawCurveTrackTypes::RCT_Float, FloatCurveNames);
	Component->SetAllowAnimCurveEvaluation(true);
	Component->SetAllowedAnimCurvesEvaluation(FloatCurveNames, true);

	PreviewScene.AddComponent(Component, FTransform::Identity);
	Component->InitAnim(true);

	// Propagates the curve filter to the PostProcess ABP instance too
	Component->RecalcRequiredCurves();

	// Inject float curves from the source animation into the pose context each frame via
	// SetPreviewCurveOverride. When driving evaluation manually (SetPosition -> TickAnimation ->
	// RefreshBoneTransforms), float curves from the source animation are not automatically
	// decompressed and fed into the pose context -- that happens as part of the full animation
	// graph evaluation which is not active in this path. SetPreviewCurveOverride writes values
	// directly into the PreviewCurveOverride map; PropagatePreviewCurve then combines them into
	// the pose context after each evaluation, making them available to the post-process ABP.
	UAnimSingleNodeInstance* SingleNodeInstance = Component->GetSingleNodeInstance();

	const FReferenceSkeleton& MeshRefSkel = TargetMesh->GetRefSkeleton();
	const int32 NumMeshBones = MeshRefSkel.GetNum();

	// Build mesh-to-skeleton bone index map. Only record face-subtree bones that exist on the
	// head mesh - those are the ones we'll write from the baked component-space transforms.
	// When multiple mesh bones share the same skeleton bone, only the first one is recorded.
	TArray<int32> MeshToSkeletonBoneIndexMap;
	TSet<FName> FaceSubtreeBoneNames;
	TSet<int32> SeenSkeletonBoneIndices;
	for (int32 MeshBoneIdx = 0; MeshBoneIdx < NumMeshBones; ++MeshBoneIdx)
	{
		const int32 SkeletonBoneIdx = TargetSkeleton->GetSkeletonBoneIndexFromMeshBoneIndex(TargetMesh, MeshBoneIdx);

		if (SkeletonBoneIdx != INDEX_NONE
			&& !SeenSkeletonBoneIndices.Contains(SkeletonBoneIdx)
			&& IsInFaceSubtree(SkeletonBoneIdx))
		{
			SeenSkeletonBoneIndices.Add(SkeletonBoneIdx);
			MeshToSkeletonBoneIndexMap.Add(SkeletonBoneIdx);
			FaceSubtreeBoneNames.Add(TargetRefSkel.GetBoneName(SkeletonBoneIdx));
		}
		else
		{
			MeshToSkeletonBoneIndexMap.Add(INDEX_NONE);
		}
	}

	TMap<FName, TArray<FVector>> PositionKeys;
	TMap<FName, TArray<FQuat>> RotationKeys;
	TMap<FName, TArray<FVector>> ScaleKeys;

	for (const FName& BoneName : FaceSubtreeBoneNames)
	{
		PositionKeys.Add(BoneName).Reserve(NumFrames);
		RotationKeys.Add(BoneName).Reserve(NumFrames);
		ScaleKeys.Add(BoneName).Reserve(NumFrames);
	}

	// Copy bone tracks verbatim from the source animation for any bone the bake won't already
	// capture (i.e. bones not in FaceSubtreeBoneNames). The head mesh's evaluated pose only
	// contains transforms for bones it has skin weights on, so without this pass-through the
	// output would be missing body bone tracks the source carried.
	IAnimationDataModel* SourceModel = SourceAnim->GetDataModelInterface().GetInterface();

	if (!SourceModel)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Warning,
			"BakeAnimation: source animation '{AnimName}' has no data model; bone tracks outside the face sub-tree will be missing from the output.",
			SourceAnim->GetName());
	}
	else
	{
		TArray<FName> SourceBodyTrackNames;
		SourceModel->GetBoneTrackNames(SourceBodyTrackNames);

		for (const FName& BoneName : SourceBodyTrackNames)
		{
			const int32 TargetBoneIdx = TargetRefSkel.FindBoneIndex(BoneName);
			if (TargetBoneIdx == INDEX_NONE || FaceSubtreeBoneNames.Contains(BoneName))
			{
				continue;
			}

			TArray<FVector>& Positions = PositionKeys.Add(BoneName);
			TArray<FQuat>& Rotations = RotationKeys.Add(BoneName);
			TArray<FVector>& Scales = ScaleKeys.Add(BoneName);
			Positions.Reserve(NumFrames);
			Rotations.Reserve(NumFrames);
			Scales.Reserve(NumFrames);

			for (int32 Frame = 0; Frame < NumFrames; ++Frame)
			{
				const FTransform LocalTransform = SourceModel->GetBoneTrackTransform(BoneName, FFrameNumber(Frame));
				Positions.Add(LocalTransform.GetTranslation());
				Rotations.Add(LocalTransform.GetRotation());
				Scales.Add(LocalTransform.GetScale3D());
			}
		}
	}

	// Bake face-subtree bone tracks from the head mesh component's evaluated pose.
	for (int32 Frame = 0; Frame < NumFrames; ++Frame)
	{
		const float Time = SourceAnim->GetTimeAtFrame(Frame);
		const float DeltaTime = 1.0f / FrameRate.AsDecimal();

		if (SingleNodeInstance)
		{
			for (const FName& CurveName : FloatCurveNames)
			{
				const float CurveValue = SourceAnim->EvaluateCurveData(CurveName, FAnimExtractContext(static_cast<double>(Time)));
				SingleNodeInstance->SetPreviewCurveOverride(CurveName, CurveValue, false);
			}
		}

		Component->SetPosition(Time, false);
		Component->TickAnimation(DeltaTime, false);
		Component->RefreshBoneTransforms();
		Component->RefreshFollowerComponents();
		Component->UpdateComponentToWorld();
		Component->FinalizeBoneTransform();

		const TArray<FTransform>& ComponentSpaceTransforms = Component->GetComponentSpaceTransforms();

		for (int32 MeshBoneIdx = 0; MeshBoneIdx < NumMeshBones && MeshBoneIdx < ComponentSpaceTransforms.Num(); ++MeshBoneIdx)
		{
			const int32 SkeletonBoneIdx = MeshToSkeletonBoneIndexMap[MeshBoneIdx];
			if (SkeletonBoneIdx == INDEX_NONE)
			{
				continue;
			}

			const FName BoneName = TargetRefSkel.GetBoneName(SkeletonBoneIdx);
			const int32 MeshParentIdx = MeshRefSkel.GetParentIndex(MeshBoneIdx);
			FTransform LocalTransform = ComponentSpaceTransforms[MeshBoneIdx];

			if (ComponentSpaceTransforms.IsValidIndex(MeshParentIdx))
			{
				LocalTransform = ComponentSpaceTransforms[MeshBoneIdx].GetRelativeTransform(ComponentSpaceTransforms[MeshParentIdx]);
			}

			PositionKeys[BoneName].Add(LocalTransform.GetTranslation());
			RotationKeys[BoneName].Add(LocalTransform.GetRotation());
			ScaleKeys[BoneName].Add(LocalTransform.GetScale3D());
		}
	}

	UAnimSequence* OutAnim = NewObject<UAnimSequence>(Outer, *AnimAssetName, RF_Public);
	ConfigureBakedAnimSequence(OutAnim, TargetSkeleton, BoneCompressionSettingsOverride);

	IAnimationDataController& Controller = OutAnim->GetController();
	Controller.OpenBracket(NSLOCTEXT("MetaHumanCrowdEditorUtilities", "BakeAnim", "Bake Animation"));
	Controller.InitializeModel();
	Controller.SetFrameRate(FrameRate);
	Controller.SetNumberOfFrames(FFrameNumber(NumFrames - 1));

	for (const TPair<FName, TArray<FVector>>& It : PositionKeys)
	{
		const FName& BoneName = It.Key;
		Controller.AddBoneCurve(BoneName);
		Controller.SetBoneTrackKeys(
			BoneName,
			It.Value,
			RotationKeys[BoneName],
			ScaleKeys[BoneName]);
	}

	Controller.NotifyPopulated();
	Controller.CloseBracket();

	OutAnim->PostEditChange();
	OutAnim->MarkPackageDirty();

	return OutAnim;
}

UAnimSequence* MergeAnimations(
	UAnimSequence* FaceAnim,
	UAnimSequence* BodyAnim,
	TNotNull<USkeleton*> TargetSkeleton,
	TObjectPtr<UAnimBoneCompressionSettings> BoneCompressionSettingsOverride,
	const FString& AnimAssetName,
	UObject* Outer,
	FName FaceRootBoneName)
{
	// At least one input must be valid
	if (!FaceAnim && !BodyAnim)
	{
		return nullptr;
	}

	// TODO: We need better handling here. For now, report warnings
	if (FaceAnim && BodyAnim)
	{
		if (FaceAnim->GetSamplingFrameRate() != BodyAnim->GetSamplingFrameRate())
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning,
				"MergeAnimations: frame rate mismatch between face ('{FaceAnimName}', {FaceAnimFps} fps) and body ('{BodyAnimName}', {BodyAnimFps} fps). "
				"Using face frame rate. Body tracks may be sampled at incorrect times.",
				FaceAnim->GetName(),
				FaceAnim->GetSamplingFrameRate().AsDecimal(),
				BodyAnim->GetName(),
				BodyAnim->GetSamplingFrameRate().AsDecimal());
		}
		if (FaceAnim->GetNumberOfSampledKeys() != BodyAnim->GetNumberOfSampledKeys())
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning,
				"MergeAnimations: length mismatch between face ('{FaceAnimName}', {FaceAnimFrameNum} frames) and body ('{BodyAnimName}', {BodyAnimFrameNum} frames). "
				"Output length matches face. If body is shorter, its last frame is held; if body is longer, extra body frames are discarded.",
				FaceAnim->GetName(),
				FaceAnim->GetNumberOfSampledKeys(),
				BodyAnim->GetName(),
				BodyAnim->GetNumberOfSampledKeys());
		}
	}

	UAnimSequence* PrimaryAnim = FaceAnim ? FaceAnim : BodyAnim;
	const FFrameRate FrameRate = PrimaryAnim->GetSamplingFrameRate();
	const int32 NumFrames = PrimaryAnim->GetNumberOfSampledKeys();
	if (NumFrames <= 0)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "MergeAnimations: primary animation '{AnimName}' has no sampled keys.", PrimaryAnim->GetName());
		return nullptr;
	}

	TMap<FName, TArray<FVector>> PositionKeys;
	TMap<FName, TArray<FQuat>> RotationKeys;
	TMap<FName, TArray<FVector>> ScaleKeys;

	// Body tracks are written first. Face tracks (below) write second and overwrite the body
	// values for any shared bone. When FaceRootBoneName is set, the face track list is filtered
	// to just that bone and its descendants, limiting which shared bones get overwritten. When
	// FaceRootBoneName is NAME_None, ALL face tracks overwrite their body-track counterparts.
	if (BodyAnim)
	{
		IAnimationDataModel* BodyModel = BodyAnim->GetDataModelInterface().GetInterface();
		const int32 BodyNumFrames = BodyAnim->GetNumberOfSampledKeys();
		const USkeleton* BodySkeleton = BodyAnim->GetSkeleton();

		if (BodyNumFrames <= 0)
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "MergeAnimations: BodyAnim '{BodyAnimName}' has no sampled keys - skipping body tracks.", BodyAnim->GetName());
		}
		else if (!BodyModel)
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "MergeAnimations: BodyAnim '{BodyAnimName}' has no data model - skipping body tracks", BodyAnim->GetName());
		}
		else if (!BodySkeleton)
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "MergeAnimations: BodyAnim '{BodyAnimName}' has no skeleton - skipping body tracks", BodyAnim->GetName());
		}
		else
		{
			TArray<FName> BodyTrackNames;
			BodyModel->GetBoneTrackNames(BodyTrackNames);

			// If the body animation has a RetargetSourceAsset, its stored bone track data is
			// encoded relative to that retarget source mesh's rest pose, not the skeleton's.
			// Use it as the source rest reference so our change-of-basis matches what the
			// runtime would do when evaluating the animation on the source skeleton.
			const USkeletalMesh* BodyRetargetSourceMesh = BodyAnim->GetRetargetSourceAsset().LoadSynchronous();
			const FReferenceSkeleton& SourceRefSkel = BodyRetargetSourceMesh
				? BodyRetargetSourceMesh->GetRefSkeleton()
				: BodySkeleton->GetReferenceSkeleton();
			const FReferenceSkeleton& TargetRefSkel = TargetSkeleton->GetReferenceSkeleton();

			for (const FName& BoneName : BodyTrackNames)
			{
				const int32 TargetBoneIdx = TargetRefSkel.FindBoneIndex(BoneName);
				const int32 SourceBoneIdx = SourceRefSkel.FindBoneIndex(BoneName);

				if (TargetBoneIdx == INDEX_NONE || SourceBoneIdx == INDEX_NONE)
				{
					continue;
				}

				TArray<FVector>& Positions = PositionKeys.Add(BoneName);
				TArray<FQuat>& Rotations = RotationKeys.Add(BoneName);
				TArray<FVector>& Scales = ScaleKeys.Add(BoneName);
				Positions.Reserve(NumFrames);
				Rotations.Reserve(NumFrames);
				Scales.Reserve(NumFrames);

				// Express the animated local transform as if it were authored on the target skeleton's rest pose.
				// final = target_rest * (source_rest^-1 * anim_local).
				const FTransform SourceRest = SourceRefSkel.GetRawRefBonePose()[SourceBoneIdx];
				const FTransform TargetRest = TargetRefSkel.GetRawRefBonePose()[TargetBoneIdx];
				const FTransform InvSourceRest = SourceRest.Inverse();

				for (int32 Frame = 0; Frame < NumFrames; ++Frame)
				{
					const int32 BodyFrame = FMath::Min(Frame, BodyNumFrames - 1);
					const FTransform LocalTransform = BodyModel->GetBoneTrackTransform(BoneName, FFrameNumber(BodyFrame));
					const FTransform Rebased = TargetRest * (InvSourceRest * LocalTransform);

					Positions.Add(Rebased.GetTranslation());
					Rotations.Add(Rebased.GetRotation());
					Scales.Add(Rebased.GetScale3D());
				}
			}
		}
	}

	if (FaceAnim)
	{
		IAnimationDataModel* FaceModel = FaceAnim->GetDataModelInterface().GetInterface();
		USkeleton* FaceSkeleton = FaceAnim->GetSkeleton();

		if (!FaceModel)
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "MergeAnimations: FaceAnim '{FaceAnim}' has no data model - skipping face tracks", FaceAnim->GetName());
		}
		else if (!FaceSkeleton)
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "MergeAnimations: FaceAnim '{FaceAnim}' has no skeleton - skipping face tracks", FaceAnim->GetName());
		}
		else
		{
			TArray<FName> FaceTrackNames;
			FaceModel->GetBoneTrackNames(FaceTrackNames);

			// If FaceRootBoneName is set, restrict face tracks to that bone and its children.
			// This prevents face bake data from overriding body animation on shared bones
			// (e.g. neck, spine) that the body animation should own.
			if (FaceRootBoneName != NAME_None)
			{
				const FReferenceSkeleton& FaceRefSkel = FaceSkeleton->GetReferenceSkeleton();
				const int32 FaceRootBoneIdx = FaceRefSkel.FindBoneIndex(FaceRootBoneName);

				if (FaceRootBoneIdx != INDEX_NONE)
				{
					// Descendants only: the face root bone itself comes from the body animation
					// so the neck/head chain stays continuous with body motion.
					FaceTrackNames.RemoveAll([&](const FName& BoneName)
					{
						const int32 BoneIdx = FaceRefSkel.FindBoneIndex(BoneName);
						return BoneIdx == INDEX_NONE || !FaceRefSkel.BoneIsChildOf(BoneIdx, FaceRootBoneIdx);
					});
				}
				else
				{
					UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "MergeAnimations: FaceRootBoneName '{FaceRootBoneName}' not found in face skeleton - using all face tracks",
						FaceRootBoneName.ToString());
				}
			}

			// If the face animation has a RetargetSourceAsset, its stored bone track data is
			// encoded relative to that retarget source mesh's rest pose, not the skeleton's.
			// Use it as the source rest reference so our change-of-basis matches what the
			// runtime would do when evaluating the animation on the source skeleton.
			const USkeletalMesh* FaceRetargetSourceMesh = FaceAnim->GetRetargetSourceAsset().LoadSynchronous();
			const FReferenceSkeleton& SourceRefSkel = FaceRetargetSourceMesh
				? FaceRetargetSourceMesh->GetRefSkeleton()
				: FaceSkeleton->GetReferenceSkeleton();
			const FReferenceSkeleton& TargetRefSkel = TargetSkeleton->GetReferenceSkeleton();

			for (const FName& BoneName : FaceTrackNames)
			{
				const int32 TargetBoneIdx = TargetRefSkel.FindBoneIndex(BoneName);
				const int32 SourceBoneIdx = SourceRefSkel.FindBoneIndex(BoneName);

				if (TargetBoneIdx == INDEX_NONE || SourceBoneIdx == INDEX_NONE)
				{
					continue;
				}

				TArray<FVector>& Positions = PositionKeys.Add(BoneName);
				TArray<FQuat>& Rotations = RotationKeys.Add(BoneName);
				TArray<FVector>& Scales = ScaleKeys.Add(BoneName);
				Positions.Reserve(NumFrames);
				Rotations.Reserve(NumFrames);
				Scales.Reserve(NumFrames);

				const FTransform SourceRest = SourceRefSkel.GetRawRefBonePose()[SourceBoneIdx];
				const FTransform TargetRest = TargetRefSkel.GetRawRefBonePose()[TargetBoneIdx];
				const FTransform InvSourceRest = SourceRest.Inverse();

				for (int32 Frame = 0; Frame < NumFrames; ++Frame)
				{
					const FTransform LocalTransform = FaceModel->GetBoneTrackTransform(BoneName, FFrameNumber(Frame));
					const FTransform Rebased = TargetRest * (InvSourceRest * LocalTransform);

					Positions.Add(Rebased.GetTranslation());
					Rotations.Add(Rebased.GetRotation());
					Scales.Add(Rebased.GetScale3D());
				}
			}
		}
	}

	UAnimSequence* OutAnim = NewObject<UAnimSequence>(Outer, *AnimAssetName, RF_Public);
	ConfigureBakedAnimSequence(OutAnim, TargetSkeleton, BoneCompressionSettingsOverride);

	IAnimationDataController& Controller = OutAnim->GetController();
	Controller.OpenBracket(NSLOCTEXT("MetaHumanCrowdEditorPipeline", "MergeAnim", "Merge Animations"));
	Controller.InitializeModel();
	Controller.SetFrameRate(FrameRate);
	Controller.SetNumberOfFrames(FFrameNumber(NumFrames - 1));

	for (const TPair<FName, TArray<FVector>>& It : PositionKeys)
	{
		const FName& BoneName = It.Key;
		Controller.AddBoneCurve(BoneName);
		Controller.SetBoneTrackKeys(BoneName, It.Value, RotationKeys[BoneName], ScaleKeys[BoneName]);
	}

	Controller.NotifyPopulated();
	Controller.CloseBracket();

	OutAnim->PostEditChange();
	OutAnim->MarkPackageDirty();

	return OutAnim;
}

int32 ResolveBundleMaterialIndex(const FMetaHumanCrowdMeshGeometryBundle& Bundle, int32 LODIndex, int32 PGIndex)
{
	if (Bundle.LODMaterialMaps.IsValidIndex(LODIndex))
	{
		const TArray<int32>& Map = Bundle.LODMaterialMaps[LODIndex];
		if (Map.IsValidIndex(PGIndex) && Map[PGIndex] != INDEX_NONE)
		{
			return Map[PGIndex];
		}
	}
	return PGIndex; // identity fallback, matches the engine contract for empty LODMaterialMap
}

void ExtractGeometryBundle(
	TNotNull<const USkeletalMesh*> Mesh,
	FMetaHumanCrowdMeshGeometryBundle& OutBundle)
{
	OutBundle.RefSkeleton = Mesh->GetRefSkeleton();
	OutBundle.Materials = Mesh->GetMaterials();

	const int32 NumLODs = Mesh->GetLODNum();
	OutBundle.MeshDescriptions.SetNum(NumLODs);
	OutBundle.LODMaterialMaps.SetNum(NumLODs);

	for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
	{
		const FMeshDescription* MeshDesc = Mesh->GetMeshDescription(LODIndex);
		if (MeshDesc)
		{
			OutBundle.MeshDescriptions[LODIndex] = *MeshDesc;
		}
		else if (const FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel())
		{
			if (ImportedModel->LODModels.IsValidIndex(LODIndex))
			{
				// Reconstruct MeshDescription from the ImportedModel if source wasn't available
				FMeshDescription ReconstructedMD;
				const USkeletalMesh* MeshPtr = Mesh;
				ImportedModel->LODModels[LODIndex].GetMeshDescription(const_cast<USkeletalMesh*>(MeshPtr), LODIndex, ReconstructedMD);
				OutBundle.MeshDescriptions[LODIndex] = MoveTemp(ReconstructedMD);
			}
		}

		// Snapshot the per-LOD section->material remap. This is the authoritative source of
		// per-section material identity under the geometry-bundle contract; PG slot names in
		// the MeshDescriptions are preserved as imported (they may be raw FBX import names
		// that do not match the canonical MaterialSlotName of the section's actual material).
		// Reconstruction resolves section materials via LODMaterialMaps[LOD][PGIndex].
		if (const FSkeletalMeshLODInfo* LODInfo = Mesh->GetLODInfo(LODIndex))
		{
			OutBundle.LODMaterialMaps[LODIndex] = LODInfo->LODMaterialMap;
		}
	}
}

bool PopulateSkeletalMeshFromMeshDescriptions(
	TNotNull<USkeletalMesh*> InSkeletalMesh,
	TArray<FMeshDescription>&& InMeshDescriptions,
	const FReferenceSkeleton& InReferenceSkeleton,
	TConstArrayView<FSkeletalMaterial> InMaterials)
{
	if (InSkeletalMesh->GetLODNum() != 0)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error,
			"PopulateSkeletalMeshFromMeshDescriptions: Skeletal mesh '{Mesh}' is not empty",
			InSkeletalMesh->GetPathName());
		return false;
	}

	if (InMeshDescriptions.IsEmpty())
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error,
			"PopulateSkeletalMeshFromMeshDescriptions: No mesh descriptions given");
		return false;
	}

	InSkeletalMesh->SetMaterials(TArray<FSkeletalMaterial>{InMaterials});

	{
		UE::MetaHuman::CrowdEditorUtilities::FScopedSkeletalMeshChange ScopedSkeletalMeshChange(InSkeletalMesh);
		InSkeletalMesh->PreEditChange(nullptr);
		InSkeletalMesh->SetRefSkeleton(InReferenceSkeleton);
		InSkeletalMesh->CalculateInvRefMatrices();

		for (int32 LODIndex = 0; LODIndex < InMeshDescriptions.Num(); ++LODIndex)
		{
			FSkeletalMeshLODInfo& SkeletalLODInfo = InSkeletalMesh->AddLODInfo();
			InSkeletalMesh->GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());

			SkeletalLODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
			SkeletalLODInfo.ReductionSettings.NumOfVertPercentage = 1.0f;
			SkeletalLODInfo.ReductionSettings.MaxDeviationPercentage = 0.0f;
			SkeletalLODInfo.LODHysteresis = 0.02f;
			SkeletalLODInfo.BuildSettings.bRecomputeNormals = false;
			SkeletalLODInfo.BuildSettings.bRecomputeTangents = false;

			InSkeletalMesh->CreateMeshDescription(LODIndex, MoveTemp(InMeshDescriptions[LODIndex]));
			InSkeletalMesh->CommitMeshDescription(LODIndex);
		}
	}

	return true;
}

USkeletalMesh* ConstructMeshFromBundle(
	const FMetaHumanCrowdMeshGeometryBundle& Bundle,
	const FMeshConstructionParams& Params,
	UObject* Outer,
	FName MeshName)
{

	if (Params.LODsToKeep.IsEmpty() || !Params.TargetSkeleton)
	{
		return nullptr;
	}

	// Validate that all requested LODs exist in the bundle
	for (int32 LODIndex : Params.LODsToKeep)
	{
		if (!Bundle.MeshDescriptions.IsValidIndex(LODIndex) || Bundle.MeshDescriptions[LODIndex].Vertices().Num() == 0)
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "ConstructMeshFromBundle: Requested LOD {LODIndex} not available in bundle (has {NumLODs} LODs). Skipping mesh.",
				LODIndex, Bundle.MeshDescriptions.Num());
			return nullptr;
		}
	}

	// Step 1: Select LODs from bundle, creating deep copies we can modify.
	//
	// Invariant: the first entry of Params.LODsToKeep becomes SelectedLODs[0], which the
	// engine build treats as LOD 0 and passes through AdjustImportDataFaceMaterialIndex
	// (which only runs for LOD 0). Step 5b-remap builds CompactedMaterials by walking
	// SelectedLODs in order, so CompactedMaterials entries for SelectedLODs[0]'s PGs appear
	// at their PG IDs first. This keeps MaterialRemap identity in the engine's LOD 0 name
	// match, leaving Face.MatIndex == PG ID, which Step 11 relies on. Callers should pass
	// LODsToKeep in ascending order from the lowest source LOD they want.
	TArray<FMeshDescription> SelectedLODs;
	SelectedLODs.Reserve(Params.LODsToKeep.Num());
	for (int32 LODIndex : Params.LODsToKeep)
	{
		SelectedLODs.Add(Bundle.MeshDescriptions[LODIndex]);
	}


	// Step 2: Collect all bones referenced by skin weights across selected LODs
	TSet<FName> ReferencedBoneNames;
	for (const FMeshDescription& MeshDesc : SelectedLODs)
	{
		FSkeletalMeshConstAttributes Attrs(MeshDesc);
		if (!Attrs.HasBones())
		{
			continue;
		}

		FSkinWeightsVertexAttributesConstRef SkinWeights = Attrs.GetVertexSkinWeights();
		FSkeletalMeshAttributesShared::FBoneNameAttributesConstRef BoneNames = Attrs.GetBoneNames();

		if (!SkinWeights.IsValid() || !BoneNames.IsValid())
		{
			continue;
		}

		for (const FVertexID VertexID : MeshDesc.Vertices().GetElementIDs())
		{
			FVertexBoneWeightsConst Weights = SkinWeights.Get(VertexID);
			for (int32 Idx = 0; Idx < Weights.Num(); ++Idx)
			{
				const FBoneIndexType BoneIdx = Weights[Idx].GetBoneIndex();
				FBoneID BoneID(BoneIdx);
				if (Attrs.Bones().IsValid(BoneID))
				{
					ReferencedBoneNames.Add(BoneNames.Get(BoneID));
				}
			}
		}
	}

	// Step 3: Add ancestor chains
	const FReferenceSkeleton& SourceRefSkel = Bundle.RefSkeleton;
	TSet<int32> BoneIndicesToKeep;
	for (const FName& BoneName : ReferencedBoneNames)
	{
		int32 BoneIdx = SourceRefSkel.FindRawBoneIndex(BoneName);
		while (BoneIdx != INDEX_NONE && !BoneIndicesToKeep.Contains(BoneIdx))
		{
			BoneIndicesToKeep.Add(BoneIdx);
			BoneIdx = SourceRefSkel.GetParentIndex(BoneIdx);
		}
	}

	// Step 3b: Force-keep bones that are part of the animation chain even if no geometry
	// is weighted to them (e.g. neck_01/02 and head on a body mesh). Without these the
	// pruned mesh has no place to put the head animation track at runtime, which makes
	// ISKM-driven head positions disagree with ABP-driven ones.
	for (const FName& ForceKeepName : Params.ForceKeepBoneNames)
	{
		int32 BoneIdx = SourceRefSkel.FindRawBoneIndex(ForceKeepName);
		while (BoneIdx != INDEX_NONE && !BoneIndicesToKeep.Contains(BoneIdx))
		{
			BoneIndicesToKeep.Add(BoneIdx);
			BoneIdx = SourceRefSkel.GetParentIndex(BoneIdx);
		}
	}

	// Step 4: Build pruned RefSkeleton (sorted by original index to preserve hierarchy order)
	TArray<int32> SortedBoneIndices = BoneIndicesToKeep.Array();
	SortedBoneIndices.Sort();

	// Map from original bone index to pruned bone index
	TMap<int32, int32> OldToNewBoneIndex;
	FReferenceSkeleton PrunedRefSkel;
	{
		FReferenceSkeletonModifier Modifier(PrunedRefSkel, Params.TargetSkeleton);
		for (int32 NewIdx = 0; NewIdx < SortedBoneIndices.Num(); ++NewIdx)
		{
			const int32 OldIdx = SortedBoneIndices[NewIdx];
			OldToNewBoneIndex.Add(OldIdx, NewIdx);

			FMeshBoneInfo BoneInfo;
			BoneInfo.Name = SourceRefSkel.GetBoneName(OldIdx);

			const int32 OldParentIdx = SourceRefSkel.GetParentIndex(OldIdx);
			BoneInfo.ParentIndex = (OldParentIdx != INDEX_NONE) ? OldToNewBoneIndex.FindRef(OldParentIdx) : INDEX_NONE;

			Modifier.Add(BoneInfo, SourceRefSkel.GetRawRefBonePose()[OldIdx]);
		}
	}

	// Step 5: Remap bone indices in the MeshDescriptions
	for (FMeshDescription& MeshDesc : SelectedLODs)
	{
		FSkeletalMeshAttributes Attrs(MeshDesc);
		if (!Attrs.HasBones())
		{
			continue;
		}

		// Rebuild bone elements to match pruned skeleton
		FSkeletalMeshAttributes::FBoneArray& Bones = Attrs.Bones();
		FSkeletalMeshAttributesShared::FBoneNameAttributesRef BoneNames = Attrs.GetBoneNames();
		FSkeletalMeshAttributes::FBoneParentIndexAttributesRef BoneParents = Attrs.GetBoneParentIndices();
		FSkeletalMeshAttributes::FBonePoseAttributesRef BonePoses = Attrs.GetBonePoses();

		// Collect existing bone data.
		//
		// OriginalIndex is the raw FBoneID value, not an iteration ordinal. Skin weights
		// store bone indices as FBoneIndexType values that correspond to FBoneID element
		// IDs, so any remap keyed from skin-weight indices must key on the same FBoneID
		// values. If bones have ever been deleted without compaction, element IDs are
		// sparse and an ordinal counter would desync from what skin weights reference.
		struct FBoneData
		{
			FName Name;
			int32 OriginalIndex;
		};
		TArray<FBoneData> ExistingBones;
		TMap<FName, int32> BoneNameToOrigIndex;
		for (const FBoneID BoneID : Bones.GetElementIDs())
		{
			FBoneData& Data = ExistingBones.AddDefaulted_GetRef();
			Data.Name = BoneNames.Get(BoneID);
			Data.OriginalIndex = BoneID.GetValue();
			BoneNameToOrigIndex.Add(Data.Name, Data.OriginalIndex);
		}

		// Build remap from old MeshDescription bone index to new pruned index
		TMap<FBoneIndexType, FBoneIndexType> MDOldToNew;
		for (const FBoneData& Data : ExistingBones)
		{
			const int32 OrigRefSkelIdx = SourceRefSkel.FindRawBoneIndex(Data.Name);
			if (OrigRefSkelIdx != INDEX_NONE)
			{
				const int32* NewIdx = OldToNewBoneIndex.Find(OrigRefSkelIdx);
				if (NewIdx)
				{
					MDOldToNew.Add(static_cast<FBoneIndexType>(Data.OriginalIndex), static_cast<FBoneIndexType>(*NewIdx));
				}
			}
		}

		// Remap skin weights
		FSkinWeightsVertexAttributesRef SkinWeights = Attrs.GetVertexSkinWeights();
		if (SkinWeights.IsValid())
		{
			UE::AnimationCore::FBoneWeightsSettings Settings;
			Settings.SetNormalizeType(UE::AnimationCore::EBoneWeightNormalizeType::Always);

			for (const FVertexID VertexID : MeshDesc.Vertices().GetElementIDs())
			{
				const FVertexBoneWeights OldWeights = SkinWeights.Get(VertexID);
				TArray<UE::AnimationCore::FBoneWeight> NewWeightArray;
				for (int32 Idx = 0; Idx < OldWeights.Num(); ++Idx)
				{
					const FBoneIndexType OldBoneIdx = OldWeights[Idx].GetBoneIndex();
					const FBoneIndexType* NewBoneIdx = MDOldToNew.Find(OldBoneIdx);
					if (NewBoneIdx)
					{
						NewWeightArray.Add(UE::AnimationCore::FBoneWeight(*NewBoneIdx, OldWeights[Idx].GetWeight()));
					}
				}
				SkinWeights.Set(VertexID, UE::AnimationCore::FBoneWeights::Create(NewWeightArray, Settings));
			}
		}

		// Reset bone elements to match pruned skeleton
		Bones.Reset(PrunedRefSkel.GetRawBoneNum());
		for (int32 NewBoneIdx = 0; NewBoneIdx < PrunedRefSkel.GetRawBoneNum(); ++NewBoneIdx)
		{
			const FBoneID NewBoneID = Attrs.CreateBone();
			BoneNames.Set(NewBoneID, PrunedRefSkel.GetBoneName(NewBoneIdx));
			BoneParents.Set(NewBoneID, PrunedRefSkel.GetParentIndex(NewBoneIdx));
			BonePoses.Set(NewBoneID, PrunedRefSkel.GetRawRefBonePose()[NewBoneIdx]);
		}
	}

	// Step 5a: Canonicalise PG slot names on the local SelectedLODs working copies.
	//
	// Under the Option B geometry-bundle contract, PG slot names in Bundle.MeshDescriptions
	// are raw FBX import names; per-section material identity is the resolved Bundle.Materials
	// index (via ResolveBundleMaterialIndex). The downstream steps (5b section removal,
	// 5b-remap material union, engine build) all key off PG slot names, so we rewrite each
	// PG's slot name to Bundle.Materials[Resolved].MaterialSlotName once up front, on the
	// local working copies. The Bundle itself is not mutated.
	//
	// Canonical names are preserved through MeshDesc.Compact() (slot names are attribute
	// values keyed by post-compact PG IDs), so section removal in Step 5b is compact-safe
	// without needing to re-resolve through LODMaterialMaps after Compact.
	//
	// Edge case: when Bundle.Materials[k].MaterialSlotName != ImportedMaterialSlotName (user
	// renamed a slot in the editor after import), the engine's LOD 0 name-match
	// (AdjustImportDataFaceMaterialIndex) will fail to find our canonical MaterialSlotName
	// among ImportedMaterialSlotNames and early-return, leaving Face.MatIndex == PG ID.
	// Step 11 then writes an identity LODMaterialMap for that LOD which resolves correctly
	// at render time. A warning is logged by the engine in that case but the final section
	// -> material assignment is correct.
	for (int32 SelIdx = 0; SelIdx < SelectedLODs.Num(); ++SelIdx)
	{
		const int32 SrcLODIndex = Params.LODsToKeep[SelIdx];
		FMeshDescription& MD = SelectedLODs[SelIdx];
		if (MD.PolygonGroups().Num() == 0)
		{
			continue;
		}
		FStaticMeshAttributes Attrs(MD);
		TPolygonGroupAttributesRef<FName> SlotNames = Attrs.GetPolygonGroupMaterialSlotNames();
		for (const FPolygonGroupID PGID : MD.PolygonGroups().GetElementIDs())
		{
			const int32 BundleMatIdx = ResolveBundleMaterialIndex(Bundle, SrcLODIndex, PGID.GetValue());
			if (Bundle.Materials.IsValidIndex(BundleMatIdx))
			{
				SlotNames.Set(PGID, Bundle.Materials[BundleMatIdx].MaterialSlotName);
			}
			// If out of range, leave the PG slot name as-is; the fallback path in Step 5b-remap
			// will surface a warning and synthesise a stub material.
		}
	}

	// Step 5b: Remove sections from MeshDescriptions before building the skeletal mesh.
	// This must happen before SafeEnableNanite (Step 8) which replaces translucent materials,
	// so we can still detect them from the original Bundle.Materials.
	//
	// PG slot names were canonicalised in Step 5a, so matching SlotNames.Get(PGID) against
	// Params.SectionsToRemove works directly. For the translucency check we resolve the
	// material by slot-name lookup into Bundle.Materials.
	if (!Params.SectionsToRemove.IsEmpty() || Params.bRemoveTranslucentSections)
	{
		// Build a canonical slot-name -> Bundle.Materials index map for the translucency check.
		TMap<FName, int32> BundleSlotNameToIndex;
		if (Params.bRemoveTranslucentSections)
		{
			BundleSlotNameToIndex.Reserve(Bundle.Materials.Num());
			for (int32 MatIdx = 0; MatIdx < Bundle.Materials.Num(); ++MatIdx)
			{
				BundleSlotNameToIndex.FindOrAdd(Bundle.Materials[MatIdx].MaterialSlotName, MatIdx);
			}
		}

		for (FMeshDescription& MeshDesc : SelectedLODs)
		{
			FStaticMeshAttributes StaticAttrs(MeshDesc);
			TPolygonGroupAttributesConstRef<FName> SlotNames = StaticAttrs.GetPolygonGroupMaterialSlotNames();

			// Collect polygons in polygon groups that should be removed.
			// We delete polygons (not triangles) because DeletePolygons correctly
			// cleans up contained triangles and then orphaned polygon groups.
			TArray<FPolygonID> PolygonsToDelete;
			for (const FPolygonGroupID PolygonGroupID : MeshDesc.PolygonGroups().GetElementIDs())
			{
				const FName SlotName = SlotNames.Get(PolygonGroupID);
				bool bShouldRemove = Params.SectionsToRemove.Contains(SlotName);

				if (!bShouldRemove && Params.bRemoveTranslucentSections)
				{
					// Check original material blend mode (before Nanite replacement)
					if (const int32* BundleMatIdx = BundleSlotNameToIndex.Find(SlotName))
					{
						if (Bundle.Materials[*BundleMatIdx].MaterialInterface &&
							!Nanite::IsSupportedBlendMode(*Bundle.Materials[*BundleMatIdx].MaterialInterface))
						{
							bShouldRemove = true;
						}
					}
				}

				if (bShouldRemove)
				{
					for (const FPolygonID PolygonID : MeshDesc.Polygons().GetElementIDs())
					{
						if (MeshDesc.GetPolygonPolygonGroup(PolygonID) == PolygonGroupID)
						{
							PolygonsToDelete.Add(PolygonID);
						}
					}
				}
			}

			if (PolygonsToDelete.Num() > 0)
			{
				MeshDesc.DeletePolygons(PolygonsToDelete);
				FElementIDRemappings Remappings;
				MeshDesc.Compact(Remappings);
			}
		}
	}

	// Step 5b-remap: Build a union materials array across all selected LODs keyed by the
	// (canonical) PG slot name. The PG-to-compacted mapping is captured per LOD for use
	// in LODMaterialMap later.
	//
	// Since Step 5a canonicalised PG slot names to MaterialSlotName, slot-name lookup into
	// Bundle.Materials resolves unambiguously. This is compact-safe (slot-name attributes
	// survive MeshDesc.Compact()) and avoids needing to track post-compact PG-ID remapping.
	//
	// CompactedMaterials entries are copies of Bundle.Materials entries, preserving both
	// MaterialSlotName and ImportedMaterialSlotName. The engine's LOD 0 name-based
	// AdjustImportDataFaceMaterialIndex matches PG slot names (now canonical) against
	// CompactedMaterials[i].ImportedMaterialSlotName; if those diverge, the engine
	// early-returns leaving Face.MatIndex == PG ID and Step 11's LODMaterialMap resolves
	// the section to the correct CompactedMaterials entry at render time.
	//
	// This remap is necessary because:
	// - The engine sets Face.MatIndex = PolygonGroupID.GetValue() (from CreateFromMeshDescription)
	// - AdjustImportDataFaceMaterialIndex only does name-based remapping for LOD 0
	// - For LOD 1+, Section.MaterialIndex = PG ID directly, which may not match the materials array
	// - LODMaterialMap on the output mesh corrects this at render time (set in Step 11)
	TArray<FSkeletalMaterial> CompactedMaterials;
	TArray<TArray<int32>> PerLODMaterialRemap; // PerLODMaterialRemap[selLODIdx][PG_ID] = CompactedMaterials index
	{
		// Build canonical slot-name -> Bundle.Materials index map for fast lookup.
		TMap<FName, int32> BundleSlotNameToIndex;
		BundleSlotNameToIndex.Reserve(Bundle.Materials.Num());
		for (int32 MatIdx = 0; MatIdx < Bundle.Materials.Num(); ++MatIdx)
		{
			BundleSlotNameToIndex.FindOrAdd(Bundle.Materials[MatIdx].MaterialSlotName, MatIdx);
		}

		// Dedup across LODs by canonical slot name; first-seen wins.
		TMap<FName, int32> SlotNameToCompactedIdx;

		PerLODMaterialRemap.SetNum(SelectedLODs.Num());
		for (int32 SelIdx = 0; SelIdx < SelectedLODs.Num(); ++SelIdx)
		{
			const FMeshDescription& MD = SelectedLODs[SelIdx];
			FStaticMeshAttributes Attrs(const_cast<FMeshDescription&>(MD));
			TPolygonGroupAttributesConstRef<FName> SlotNames = Attrs.GetPolygonGroupMaterialSlotNames();

			TArray<int32>& Remap = PerLODMaterialRemap[SelIdx];
			Remap.Init(INDEX_NONE, MD.PolygonGroups().Num());

			for (const FPolygonGroupID PGID : MD.PolygonGroups().GetElementIDs())
			{
				const FName SlotName = SlotNames.Get(PGID);

				// Skip empty polygon groups. They produce no sections in the constructed mesh,
				// so their slot doesn't need an entry in CompactedMaterials. Adding one
				// regardless creates a phantom material slot with no sections referencing it.
				//
				// Skipping is compact-safe: orphan PGs may share their slot name with a
				// non-empty PG (e.g. fitted outfits where the Chaos export creates one PG per
				// source LOD per material, all with the same canonical slot name -- only the
				// PG corresponding to this LOD has geometry). The non-empty PG will register
				// the slot in CompactedMaterials; this empty one piggybacks on that entry via
				// the SlotNameToCompactedIdx lookup below.
				int32 NumPolys = 0;
				for (const FPolygonID PolyID : MD.Polygons().GetElementIDs())
				{
					if (MD.GetPolygonPolygonGroup(PolyID) == PGID)
					{
						++NumPolys;
						break;
					}
				}
				if (NumPolys == 0)
				{
					// Leave Remap at INDEX_NONE; no section references this PG.
					continue;
				}

				if (const int32* ExistingCompactedIdx = SlotNameToCompactedIdx.Find(SlotName))
				{
					Remap[PGID.GetValue()] = *ExistingCompactedIdx;
					continue;
				}

				int32 NewCompactedIdx = INDEX_NONE;
				if (const int32* BundleMatIdx = BundleSlotNameToIndex.Find(SlotName))
				{
					NewCompactedIdx = CompactedMaterials.Add(Bundle.Materials[*BundleMatIdx]);
				}
				else
				{
					// No matching Bundle.Materials entry. This happens when Step 5a left a
					// PG slot name unchanged because its resolved index was out of range,
					// i.e. the source bundle was malformed. Preserve the old fallback so a
					// bad bundle doesn't crash the pipeline.
					UE_LOGFMT(LogMetaHumanCrowdEditor, Warning,
						"[CMFB] Step5b-remap: No Bundle.Materials entry for canonical slot name '{SlotName}' (SelLOD[{SelIdx}] PG {PGID}). Synthesising stub.",
						SlotName, SelIdx, PGID.GetValue());
					FSkeletalMaterial Fallback;
					Fallback.MaterialSlotName = SlotName;
					NewCompactedIdx = CompactedMaterials.Add(Fallback);
				}

				SlotNameToCompactedIdx.Add(SlotName, NewCompactedIdx);
				Remap[PGID.GetValue()] = NewCompactedIdx;
			}
		}
	}

	// Step 5b-engine-namematch: Predict whether the engine's
	// FLODUtilities::AdjustImportDataFaceMaterialIndex name match will succeed for built LOD 0
	// during the render data build.
	//
	// It's intended to generate a mapping between imported material slot names and material slots
	// that may have been modified in engine after import. However, there are cases where it can
	// bail out and not do the remapping. We need to know at this point whether it will succeed or
	// fail, otherwise it could either double-remap the material indices or not remap at all, and
	// both of these outcomes would lead to mesh sections silently pointing to the wrong material 
	// slot.
	//
	// Even if we waited for the render data to be built, we wouldn't be able to tell by looking at
	// the render data whether it succeeded or not, so we have to replicate its logic here to find 
	// out what it's going to do.
	//
	// Due to the nature of the data, it would be complicated to ensure that it always succeeds,
	// and in fact that would still require duplicating the engine's logic, so that's not a 
	// complete solution either. Instead, we have automated tests in this module to ensure that the
	// engine behaves as this code expects, so that if the behavior ever changes we'll be notified 
	// by test failures.
	bool bEngineWillNameMatchBuiltLOD0 = false;
	if (SelectedLODs.Num() > 0 && CompactedMaterials.Num() > 1)
	{
		TSet<FName> CompactedImportedNames;
		CompactedImportedNames.Reserve(CompactedMaterials.Num());
		for (const FSkeletalMaterial& M : CompactedMaterials)
		{
			CompactedImportedNames.Add(M.ImportedMaterialSlotName);
		}

		bEngineWillNameMatchBuiltLOD0 = true;
		const FMeshDescription& BuiltLOD0MD = SelectedLODs[0];
		FStaticMeshConstAttributes Attrs(BuiltLOD0MD);
		TPolygonGroupAttributesConstRef<FName> SlotNames = Attrs.GetPolygonGroupMaterialSlotNames();
		// Engine iterates *every* PG (including empty ones), so we have to as well.
		for (const FPolygonGroupID PGID : BuiltLOD0MD.PolygonGroups().GetElementIDs())
		{
			const FName SlotName = SlotNames.Get(PGID);
			if (!CompactedImportedNames.Contains(SlotName))
			{
				bEngineWillNameMatchBuiltLOD0 = false;
				break;
			}
		}
	}

	// Step 5c: Disable shadow casting on specified sections.
	// We track which polygon group indices need shadow disabled; after the mesh is built,
	// we apply this to UserSectionsData on the LODModel so it persists across rebuilds.
	TSet<FName> ShadowDisableSlotNames;
	if (!Params.SectionsToDisableShadow.IsEmpty())
	{
		ShadowDisableSlotNames = TSet<FName>(Params.SectionsToDisableShadow);
	}

	// Step 6: Build the skeletal mesh from the prepared MeshDescriptions.
	// Everything that modifies the mesh before the build goes inside this outer scope.
	// PopulateSkeletalMeshFromMeshDescriptions has a nested scope that is a no-op here;
	// the actual build happens when this outer scope exits.
	USkeletalMesh* Mesh = NewObject<USkeletalMesh>(Outer, MeshName, RF_Public);

	{
		UE::MetaHuman::CrowdEditorUtilities::FScopedSkeletalMeshChange ScopedSkeletalMeshChange(Mesh);

		if (!PopulateSkeletalMeshFromMeshDescriptions(Mesh, MoveTemp(SelectedLODs), PrunedRefSkel, CompactedMaterials))
		{
			return nullptr;
		}

		Mesh->SetSkeleton(Params.TargetSkeleton);

		// Step 7: DNA preservation
		if (Params.bPreserveDNA && Params.SourceDNAMesh)
		{
			if (UDNAAssetUserData* SourceDNAUserData = Params.SourceDNAMesh->GetAssetUserData<UDNAAssetUserData>())
			{
				UDNAAssetUserData* NewDNAUserData = DuplicateObject(SourceDNAUserData, Mesh);
				if (NewDNAUserData && NewDNAUserData->DNAAsset)
				{
					NewDNAUserData->DNAAsset = DuplicateObject(NewDNAUserData->DNAAsset, Outer);
					NewDNAUserData->DNAAsset->SetFlags(RF_Public);

					// Calibrate DNA LODs to match the kept LODs
					if (TSharedPtr<IDNAReader> DNAReader = USkelMeshDNAUtils::GetDNAReader(Params.SourceDNAMesh))
					{
						TArray<uint16> LODsToSet;
						Algo::Transform(Params.LODsToKeep, LODsToSet, [](int32 LODIndex)
						{
							return static_cast<uint16>(LODIndex);
						});
						FDNACalibSetLODsCommand SetLODsCommand{ LODsToSet };
						TSharedPtr<FDNACalibDNAReader> OutputDNAReader = MakeShared<FDNACalibDNAReader>(DNAReader.Get());
						SetLODsCommand.Run(OutputDNAReader.Get());
						USkelMeshDNAUtils::SetDNAReader(Mesh, OutputDNAReader, EDNACopyPolicy::Alias, ERigLogicInitPolicy::Defer);
						if (NewDNAUserData->DNAAsset)
						{
							NewDNAUserData->DNAAsset->RestoreLegacyUEMHCCompatibility();
						}
					}

					Mesh->AddAssetUserData(NewDNAUserData);
				}
			}
		}

		// Step 8: Nanite
		if (Params.bEnableNanite)
		{
			SafeEnableNanite(Mesh);
		}

		// Step 9: Instancing
		if (Params.bOptimizeForInstancing)
		{
			SetOptimizeForInstancing(Mesh);
		}

		// Step 10: Bone influence limit
		if (Params.BoneInfluenceLimit > 0)
		{
			for (int32 LODIndex = 0; LODIndex < Mesh->GetLODNum(); ++LODIndex)
			{
				FSkeletalMeshLODInfo* LODInfo = Mesh->GetLODInfo(LODIndex);
				if (LODInfo)
				{
					LODInfo->BuildSettings.BoneInfluenceLimit = Params.BoneInfluenceLimit;
				}
			}
		}

		// Optional chain alignment. Done here, inside the build scope, so the body-aligned
		// ref-pose is in place when the engine builds vertex data when the scope exits.
		// MeshDescription bone poses already hold the bundle's original ref pose (written
		// in Step 4), so vertex bind data ends up authored against the original frame while
		// the persisted ref skeleton is body-aligned -- the intentional mismatch that shifts
		// skinned vertices to the aligned position at runtime.
		if (Params.AlignmentRefSkeleton && !Params.AlignmentBoneName.IsNone())
		{
			{
				FReferenceSkeletonModifier Modifier(Mesh->GetRefSkeleton(), nullptr);
				int32 SourceBoneIdx = Params.AlignmentRefSkeleton->FindRawBoneIndex(Params.AlignmentBoneName);
				while (SourceBoneIdx != INDEX_NONE)
				{
					const FName BoneName = Params.AlignmentRefSkeleton->GetBoneName(SourceBoneIdx);
					const FTransform& SourceLocalPose = Params.AlignmentRefSkeleton->GetRawRefBonePose()[SourceBoneIdx];
					const int32 MeshBoneIdx = Modifier.FindBoneIndex(BoneName);
					if (MeshBoneIdx != INDEX_NONE)
					{
						Modifier.UpdateRefPoseTransform(MeshBoneIdx, SourceLocalPose);
					}
					SourceBoneIdx = Params.AlignmentRefSkeleton->GetParentIndex(SourceBoneIdx);
				}
			}
			Mesh->CalculateInvRefMatrices();
		}

		ApplyLODScreenSizes(Mesh, Params.ScreenSizes, Params.ScreenSizeScaleFactor);
	}

	// Step 11: Populate LODMaterialMap
	//
	// For LODs 1+ (and sometimes LOD 0; see comments below), the engine leaves 
	// Section.MaterialIndex == PG ID and PerLODMaterialRemap[LOD][PG_ID] gives the correct
	// CompactedMaterials index.
	{
		FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel();
		if (ImportedModel)
		{
			for (int32 LODIndex = 0; LODIndex < ImportedModel->LODModels.Num(); ++LODIndex)
			{
				const bool bSkipBuiltLOD0Remap = (LODIndex == 0) && bEngineWillNameMatchBuiltLOD0;
				if (bSkipBuiltLOD0Remap)
				{
					// In this case, each mesh section's material index will point to the correct
					// material slot, so no LODMaterialMap is necessary.
					continue;
				}

				FSkeletalMeshLODInfo* LODInfo = Mesh->GetLODInfo(LODIndex);
				const FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[LODIndex];
				if (LODInfo && LODInfo->LODMaterialMap.IsEmpty())
				{
					LODInfo->LODMaterialMap.SetNum(LODModel.Sections.Num());
					for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); ++SectionIndex)
					{
						const int32 SectionMatIdx = LODModel.Sections[SectionIndex].MaterialIndex;
						int32 RemappedIdx = SectionMatIdx; // fallback: identity

						if (PerLODMaterialRemap.IsValidIndex(LODIndex) && PerLODMaterialRemap[LODIndex].IsValidIndex(SectionMatIdx))
						{
							const int32 LookedUp = PerLODMaterialRemap[LODIndex][SectionMatIdx];
							// Step 5b-remap stores INDEX_NONE for empty PGs (skipped from
							// CompactedMaterials). A section pointing at such a PG would mean
							// the build produced a section for a PG with zero polygons, which
							// shouldn't happen -- fall back to identity if it does, so we
							// don't write -1 into LODMaterialMap.
							if (LookedUp != INDEX_NONE)
							{
								RemappedIdx = LookedUp;
							}
						}

						LODInfo->LODMaterialMap[SectionIndex] = RemappedIdx;
					}
				}
			}
		}
	}

	// These steps unfortunately rely on data produced during the mesh build (LODModel.Sections)
	// and therefore have to be done in a separate build scope, meaning a second mesh build is 
	// needed to construct this one mesh successfully.
	//
	// Hopefully we can rework this in future to remove the need for a second build, without 
	// altering the API of this function.
	{
		UE::MetaHuman::CrowdEditorUtilities::FScopedSkeletalMeshChange ScopedSkeletalMeshChange(Mesh);

		// Step 12: Disable shadow casting on specified sections via UserSectionsData
		if (!ShadowDisableSlotNames.IsEmpty())
		{
			const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
			FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel();
			if (ImportedModel)
			{
				for (int32 LODIndex = 0; LODIndex < ImportedModel->LODModels.Num(); ++LODIndex)
				{
					FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[LODIndex];
					const FSkeletalMeshLODInfo* LODInfo = Mesh->GetLODInfo(LODIndex);
					for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); ++SectionIndex)
					{
						FSkelMeshSection& Section = LODModel.Sections[SectionIndex];
						// Resolve material index through LODMaterialMap (same path the renderer uses)
						int32 UseMaterialIndex = Section.MaterialIndex;
						if (LODInfo && LODInfo->LODMaterialMap.IsValidIndex(SectionIndex) && LODInfo->LODMaterialMap[SectionIndex] != INDEX_NONE)
						{
							UseMaterialIndex = LODInfo->LODMaterialMap[SectionIndex];
						}

						if (Materials.IsValidIndex(UseMaterialIndex))
						{
							const FName SlotName = Materials[UseMaterialIndex].MaterialSlotName;
							if (ShadowDisableSlotNames.Contains(SlotName))
							{
								FSkelMeshSourceSectionUserData& UserData =
									FSkelMeshSourceSectionUserData::GetSourceSectionUserData(LODModel.UserSectionsData, Section);
								UserData.bCastShadow = false;
								Section.bCastShadow = false;
							}
						}
					}
				}
			}
		}

		// Step 13: Enable Recompute Tangents on matching sections
		EnableRecomputeTangents(Mesh,
			Params.RecomputeTangentsLODIndexThreshold,
			Params.RecomputeTangentsMaterialSlotNames,
			Params.RecomputeTangentsVertexMaskChannel);
	}

	return Mesh;
}

// Bump this when the hashing logic below changes semantics, to invalidate
// DDC entries produced by older versions of this code.
static const FGuid ContentAddressableSkelMeshGuidVersion(0xD562E229, 0xFBE24C54, 0xA4C4113A, 0xC893DF21);

FGuid ComputeContentAddressableSkelMeshGuid(TNotNull<const USkeletalMesh*> Mesh)
{
	// Hashes the parts of a USkeletalMesh that affect FSkelMeshRenderSection::MaterialIndex
	// resolution but are NOT already reflected in USkeletalMesh::BuildDerivedDataKey:
	//   - Materials array: ordered list of (ImportedMaterialSlotName, MaterialSlotName) pairs.
	//   - LODInfo::LODMaterialMap per LOD (remapping table from section index to Materials index).
	//
	// NOTE: FSkelMeshSection::MaterialIndex values are intentionally not included. This
	// assumes no code mutates Section.MaterialIndex directly after the mesh is built.
	TArray<uint8> TempBytes;
	TempBytes.Reserve(256);
	FMemoryWriter Ar(TempBytes, /*bIsPersistent=*/ true);

	// Materials: ordered list of (ImportedMaterialSlotName, MaterialSlotName) pairs.
	const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
	int32 NumMaterials = Materials.Num();
	Ar << NumMaterials;
	for (const FSkeletalMaterial& Material : Materials)
	{
		FString ImportedSlotName = Material.ImportedMaterialSlotName.ToString();
		FString SlotName = Material.MaterialSlotName.ToString();
		Ar << ImportedSlotName;
		Ar << SlotName;
	}

	// LODMaterialMap per LOD.
	int32 LODCount = Mesh->GetLODNum();
	Ar << LODCount;
	for (int32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
	{
		if (const FSkeletalMeshLODInfo* LODInfo = Mesh->GetLODInfo(LODIndex))
		{
			// Copy to a non-const local because FArchive::operator<< takes non-const.
			TArray<int32> LODMaterialMap = LODInfo->LODMaterialMap;
			Ar << LODMaterialMap;
		}
		else
		{
			int32 Sentinel = INDEX_NONE;
			Ar << Sentinel;
		}
	}

	FSHA1 Sha;
	Sha.Update(TempBytes.GetData(), TempBytes.Num() * TempBytes.GetTypeSize());
	Sha.Final();
	uint32 Hash[5];
	Sha.GetHash(reinterpret_cast<uint8*>(Hash));
	return FGuid(HashCombine(Hash[0], Hash[4]), Hash[1], Hash[2], Hash[3]);
}

void ApplyContentAddressableSkelMeshGuid(TNotNull<USkeletalMesh*> Mesh)
{
	FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel();
	if (!ImportedModel)
	{
		return;
	}

	const FGuid ContentGuid = ComputeContentAddressableSkelMeshGuid(Mesh);
	ImportedModel->SkeletalMeshModelGUID = FGuid::Combine(ContentAddressableSkelMeshGuidVersion, ContentGuid);
	ImportedModel->bGuidIsHash = true;
}

FScopedSkeletalMeshChange::FScopedSkeletalMeshChange(TNotNull<USkeletalMesh*> InMesh)
	: Mesh(InMesh)
	, EngineScope(InMesh)
{
}

FScopedSkeletalMeshChange::~FScopedSkeletalMeshChange()
{
	// Apply the content-addressable GUID before EngineScope's destructor runs, so the
	// engine's DDC key computation in FSkeletalMeshRenderData::Cache sees the new GUID.
	//
	// EngineScope is declared last and therefore destructs after this body, which is what
	// actually triggers PostEditChange / the build when the outermost scope exits.
	ApplyContentAddressableSkelMeshGuid(Mesh);
}

UAnimSequence* CopyAnimationCurves(
	TNotNull<UAnimSequence*> FaceAnim,
	TNotNull<UAnimSequence*> BodyAnim,
	TNotNull<USkeleton*> TargetSkeleton,
	TObjectPtr<UAnimBoneCompressionSettings> BoneCompressionSettingsOverride,
	const FString& AnimAssetName,
	UObject* Outer)
{
	// TODO: We need better handling here. For now, report warnings.
	// Output frame rate and length are taken from BodyAnim; face curves are sampled at the
	// body's frame times, so a mismatch can produce subtly off-time face motion.
	if (FaceAnim->GetSamplingFrameRate() != BodyAnim->GetSamplingFrameRate())
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Warning,
			"CopyAnimationCurves: frame rate mismatch between face ('{FaceAnimName}', {FaceAnimFps} fps) and body ('{BodyAnimName}', {BodyAnimFps} fps). "
			"Using body frame rate; face curves will be resampled at the body's frame times.",
			FaceAnim->GetName(),
			FaceAnim->GetSamplingFrameRate().AsDecimal(),
			BodyAnim->GetName(),
			BodyAnim->GetSamplingFrameRate().AsDecimal());
	}
	if (FaceAnim->GetNumberOfSampledKeys() != BodyAnim->GetNumberOfSampledKeys())
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Warning,
			"CopyAnimationCurves: length mismatch between face ('{FaceAnimName}', {FaceAnimFrameNum} frames) and body ('{BodyAnimName}', {BodyAnimFrameNum} frames). "
			"Output length matches body. Face curve keys are copied verbatim with their original times: if face is longer, keys past the body duration remain in the "
			"asset but lie beyond the playable range; if face is shorter, the last key value extrapolates for the remainder of the body duration.",
			FaceAnim->GetName(),
			FaceAnim->GetNumberOfSampledKeys(),
			BodyAnim->GetName(),
			BodyAnim->GetNumberOfSampledKeys());
	}

	const FFrameRate FrameRate = BodyAnim->GetSamplingFrameRate();
	const USkeleton* BodySourceSkeleton = BodyAnim->GetSkeleton();

	if (!BodySourceSkeleton)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "CopyAnimationCurves: BodyAnim '{BodyAnimName}' has no skeleton.", BodyAnim->GetName());
		return nullptr;
	}

	IAnimationDataModel* FaceModel = FaceAnim->GetDataModelInterface().GetInterface();

	if (!FaceModel)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "CopyAnimationCurves: face animations {FaceAnimName} have no data model.", FaceAnim->GetName());
		return nullptr;
	}

	IAnimationDataModel* BodyModel = BodyAnim->GetDataModelInterface().GetInterface();

	if (!BodyModel)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "CopyAnimationCurves: body animation {BodyAnimName} have no data model.", BodyAnim->GetName());
		return nullptr;
	}

	const int32 BodyNumFrames = BodyAnim->GetNumberOfSampledKeys();
	if (BodyNumFrames <= 0)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "CopyAnimationCurves: BodyAnim '{BodyAnimName}' has no sampled keys.", BodyAnim->GetName());
		return nullptr;
	}

	UAnimSequence* OutAnim = NewObject<UAnimSequence>(Outer, *AnimAssetName, RF_Public);
	ConfigureBakedAnimSequence(OutAnim, TargetSkeleton, BoneCompressionSettingsOverride);
	
	// Propagate the body animation's retarget source to the output.
	OutAnim->RetargetSource = NAME_None;
	if (USkeletalMesh* RetargetMesh = BodyAnim->GetRetargetSourceAsset().LoadSynchronous())
	{
		OutAnim->SetRetargetSourceAsset(RetargetMesh);
	}

	IAnimationDataController& Controller = OutAnim->GetController();
	Controller.OpenBracket(NSLOCTEXT("MetaHumanCrowdEditorPipeline", "CopyAnimCurves", "Copy Animation Curves"));
	Controller.InitializeModel();
	Controller.SetFrameRate(FrameRate);
	Controller.SetNumberOfFrames(FFrameNumber(BodyNumFrames - 1));

	// Copy bone tracks verbatim from the body animation.
	TArray<FName> BodyTrackNames;
	BodyModel->GetBoneTrackNames(BodyTrackNames);

	TArray<FVector> PositionKeys;
	TArray<FQuat> RotationKeys;
	TArray<FVector> ScaleKeys;
	PositionKeys.Reserve(BodyNumFrames);
	RotationKeys.Reserve(BodyNumFrames);
	ScaleKeys.Reserve(BodyNumFrames);

	for (const FName& BoneName : BodyTrackNames)
	{
		PositionKeys.Reset();
		RotationKeys.Reset();
		ScaleKeys.Reset();

		for (int32 Frame = 0; Frame < BodyNumFrames; ++Frame)
		{
			const FTransform LocalTransform = BodyModel->GetBoneTrackTransform(BoneName, FFrameNumber(Frame));
			PositionKeys.Add(LocalTransform.GetTranslation());
			RotationKeys.Add(LocalTransform.GetRotation());
			ScaleKeys.Add(LocalTransform.GetScale3D());
		}

		Controller.AddBoneCurve(BoneName);
		Controller.SetBoneTrackKeys(BoneName, PositionKeys, RotationKeys, ScaleKeys);
	}

	// Copy float curves from the face animation. Each float curve becomes a new curve on the output.
	const FAnimationCurveData& FaceCurveData = FaceModel->GetCurveData();
	for (const FFloatCurve& FaceCurve : FaceCurveData.FloatCurves)
	{
		const FAnimationCurveIdentifier CurveId(FaceCurve.GetName(), ERawCurveTrackTypes::RCT_Float);
		Controller.AddCurve(CurveId, FaceCurve.GetCurveTypeFlags());
		Controller.SetCurveKeys(CurveId, FaceCurve.FloatCurve.GetConstRefOfKeys());
		Controller.SetCurveColor(CurveId, FaceCurve.GetColor());
	}

	Controller.NotifyPopulated();
	Controller.CloseBracket();

	OutAnim->PostEditChange();
	OutAnim->MarkPackageDirty();

	return OutAnim;
}

} // namespace UE::MetaHuman::CrowdEditorUtilities
