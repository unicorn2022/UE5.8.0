// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoHash.h"
#include "Misc/Guid.h"
#include "Misc/NotNull.h"
#include "PerQualityLevelProperties.h"
#include "SkeletalMeshTypes.h"
#include "UObject/NameTypes.h"
#include "UObject/PerPlatformProperties.h"

class UAnimSequence;
class UObject;
class USkeletalMesh;
class USkeleton;
class UAnimBoneCompressionSettings;
struct FMetaHumanCrowdMeshGeometryBundle;
struct FReferenceSkeleton;

namespace UE::MetaHuman::CrowdEditorUtilities
{
	/**
	 * Enables Nanite on a mesh, changing any settings necessary to ensure it will render correctly.
	 */
	void SafeEnableNanite(TNotNull<USkeletalMesh*> Mesh);

	/**
	 * Applies per-platform and per-quality-level MinLOD to a skeletal mesh.
	 * 
	 * Values are clamped to the valid range [0, GetLODNum() - 1] before being written to the asset.
	 */
	void ApplyMinLODToMesh(USkeletalMesh* Mesh, const FPerPlatformInt& MinLOD, const FPerQualityLevelInt& QualityLevelMinLOD);

	/** Remove unskinned bones to reduce animation cost */
	void RemoveUnusedBones(TNotNull<USkeletalMesh*> Mesh);

	/** Set bOptimizeForInstancing on all LODs so the mesh works efficiently with ISMKs */
	void SetOptimizeForInstancing(TNotNull<USkeletalMesh*> Mesh);

	/**
	 * Rebind a skeletal mesh to a different skeleton. For each LOD, skin weights on bones that do not
	 * exist in the new skeleton are redistributed to the nearest surviving ancestor bone. The mesh's
	 * reference skeleton is then rebuilt to match the new skeleton and the skeleton pointer is updated.
	 *
	 * @param Mesh			The skeletal mesh to rebind.
	 * @param NewSkeleton	The target skeleton. Must share at least one bone with the mesh.
	 * @return true if the rebind succeeded, false otherwise.
	 */
	bool TryRebindToSkeleton(TNotNull<USkeletalMesh*> Mesh, TNotNull<USkeleton*> NewSkeleton);

	/**
	 * Bake a source animation (bone tracks + float curves) through TargetMesh's post-process ABP.
	 *
	 * Per-frame evaluation drives the source animation through the mesh's post-process ABP
	 * (e.g. RigLogic) to produce final bone transforms.
	 *
	 * Output bone tracks for the strict descendants of FaceRootBoneName come from the baked
	 * head mesh pose (so RigLogic-driven facial bones are captured correctly). Tracks for all
	 * other bones - including FaceRootBoneName itself - are copied verbatim from the source
	 * animation. The face root bone is intentionally excluded from the bake so any body-driven
	 * head motion in the source survives untouched. The head mesh component only produces
	 * component-space transforms for bones it has skin weights on, so body bones aren't in its
	 * evaluated pose and must come from the source instead.
	 *
	 * @param SourceAnim		Source animation; may carry bone tracks, float curves, or both.
	 * @param TargetMesh		Mesh whose post-process ABP drives the bake (typically the head mesh).
	 * @param BoneCompressionSettingsOverride
	 *							Optional override for the output anim's BoneCompressionSettings.
	 *							If null, the engine's default animation-recorder preset is used.
	 * @param AnimAssetName		Name of the output UAnimSequence asset.
	 * @param Outer				Outer object for the new UAnimSequence.
	 * @param FaceRootBoneName	Strict descendants of this bone come from the baked head pose;
	 *							the bone itself and everything outside the sub-tree pass through
	 *							from the source animation. If NAME_None, every bone present in
	 *							the head mesh comes from the bake.
	 * @return					The baked UAnimSequence, or nullptr on failure.
	 */
	UAnimSequence* BakeAnimation(
		TNotNull<UAnimSequence*> SourceAnim,
		TNotNull<USkeletalMesh*> TargetMesh,
		TObjectPtr<UAnimBoneCompressionSettings> BoneCompressionSettingsOverride,
		const FString& AnimAssetName,
		UObject* Outer,
		FName FaceRootBoneName = NAME_None);

	/**
	 * Merge a baked face animation and/or a raw body animation into a single UAnimSequence on
	 * TargetSkeleton. Face bone tracks (filtered to FaceRootBoneName and its children when set) take
	 * priority over body bone tracks for any shared bones.
	 *
	 * Cases handled:
	 *   FaceAnim + BodyAnim  -> face bone tracks + body bone tracks
	 *   FaceAnim only        -> face bone tracks only; body bone tracks omitted
	 *   BodyAnim only        -> body bone tracks only; face bone tracks omitted
	 *
	 * @param FaceAnim			Optional pre-baked face animation (from BakeAnimation).
	 * @param BodyAnim			Optional raw body animation.
	 * @param TargetSkeleton	The merged skeleton covering all bones.
	 * @param BoneCompressionSettingsOverride
	 *							Optional override for the output anim's BoneCompressionSettings.
	 *							If null, the engine's default animation-recorder preset is used.
	 * @param AnimAssetName		Name of the output UAnimSequence asset.
	 * @param Outer				Outer object for the new UAnimSequence.
	 * @param FaceRootBoneName	If set, only face tracks from this bone and its children are used.
	 * @return					The merged UAnimSequence, or nullptr on failure.
	 */
	UAnimSequence* MergeAnimations(
		UAnimSequence* FaceAnim,
		UAnimSequence* BodyAnim,
		TNotNull<USkeleton*> TargetSkeleton,
		TObjectPtr<UAnimBoneCompressionSettings> BoneCompressionSettingsOverride,
		const FString& AnimAssetName,
		UObject* Outer,
		FName FaceRootBoneName = NAME_None);

	/** Parameters for constructing a USkeletalMesh from a geometry bundle. */
	struct FMeshConstructionParams
	{
		/** Which LOD indices to extract from the geometry bundle. */
		TArray<int32> LODsToKeep;

		/** Skeleton to rebind to. Required. */
		USkeleton* TargetSkeleton = nullptr;

		/** If true, enable Nanite rendering (replaces unsupported materials). */
		bool bEnableNanite = false;

		/** If true, set bOptimizeForInstancing on all LODs. */
		bool bOptimizeForInstancing = false;

		/** If > 0, set BoneInfluenceLimit on all LOD build settings. */
		int32 BoneInfluenceLimit = 0;

		/** Material slot names whose sections should be removed entirely. */
		TArray<FName> SectionsToRemove;

		/**
		 * Bones to keep in the pruned skeleton even if no vertices are weighted to them.
		 * Their ancestor chain up to the skeleton root is also preserved.
		 * Use for bones the runtime animation references (e.g. head/neck_01/neck_02 on a body
		 * mesh) so ISKM and ABP playback agree on world-space bone positions.
		 */
		TArray<FName> ForceKeepBoneNames;

		/** Material slot names whose sections should have shadow casting disabled. */
		TArray<FName> SectionsToDisableShadow;

		/** If true, remove any section whose material has a translucent blend mode. */
		bool bRemoveTranslucentSections = false;

		/** If true, preserve DNA from SourceDNAMesh (copy UDNAAssetUserData and calibrate LODs). */
		bool bPreserveDNA = false;

		/** Source mesh to copy DNA from (only used when bPreserveDNA is true). */
		USkeletalMesh* SourceDNAMesh = nullptr;

		/**
		 * Optional reference skeleton to align the constructed mesh's body chain against. The chain ending at
		 * AlignmentBoneName has its local ref-pose transforms overwritten from this skeleton.
		 */
		const FReferenceSkeleton* AlignmentRefSkeleton = nullptr;

		/** Optional leaf bone of the chain to align. Walks parents from this bone up to the root. */
		FName AlignmentBoneName = NAME_None;

		/** LOD screen size thresholds */
		TArray<FPerPlatformFloat> ScreenSizes;

		/** Uniform multiplier applied to every ScreenSizes entry. */
		float ScreenSizeScaleFactor = 1.0f;

		/**
		 * Recompute Tangents is enabled on every section whose material slot name appears in
		 * RecomputeTangentsMaterialSlotNames, on every LOD whose index is
		 * <= RecomputeTangentsLODIndexThreshold. 
		 *
		 * SkinCacheUsage is forced to Enabled on any LOD where at least one matching 
		 * section was found.
		 */
		int32 RecomputeTangentsLODIndexThreshold = -1;
		TArray<FName> RecomputeTangentsMaterialSlotNames;
		ESkinVertexColorChannel RecomputeTangentsVertexMaskChannel = ESkinVertexColorChannel::Green;
	};

	/**
	 * Constructs a USkeletalMesh from a geometry bundle with the given parameters.
	 *
	 * Selects LODs, prunes the RefSkeleton to only referenced bones, initialises from
	 * MeshDescriptions, rebinds to TargetSkeleton, and applies post-processing (Nanite,
	 * instancing, bone limits, section removal, DNA preservation).
	 *
	 * @param Bundle	The geometry bundle to construct from.
	 * @param Params	Construction parameters.
	 * @param Outer		Outer object for the new mesh.
	 * @param MeshName	Optional name for the new mesh.
	 * @return The constructed mesh, or nullptr on failure.
	 */
	USkeletalMesh* ConstructMeshFromBundle(
		const FMetaHumanCrowdMeshGeometryBundle& Bundle,
		const FMeshConstructionParams& Params,
		UObject* Outer,
		FName MeshName = NAME_None);

	/**
	 * Extracts a geometry bundle from an existing USkeletalMesh by reading all LODs'
	 * MeshDescriptions, the RefSkeleton, the Materials array, and the per-LOD material maps.
	 *
	 * MeshDescription polygon group slot names are preserved as imported (they may be raw
	 * FBX names that do not match the canonical MaterialSlotName of the section's actual
	 * material). Consumers should use LODMaterialMaps to resolve each PG to its Materials
	 * index via ResolveBundleMaterialIndex.
	 */
	void ExtractGeometryBundle(
		TNotNull<const USkeletalMesh*> Mesh,
		FMetaHumanCrowdMeshGeometryBundle& OutBundle);

	/**
	 * Resolve a polygon-group index in Bundle.MeshDescriptions[LODIndex] to an index into
	 * Bundle.Materials. Mirrors the engine's render-time material resolution:
	 * LODMaterialMap[SectionIdx] with identity fallback when the map is empty or INDEX_NONE.
	 *
	 * The engine sets Face.MatIndex = PolygonGroupID.GetValue() (see
	 * FSkeletalMeshImportData::CreateFromMeshDescription), so for any MeshDescription produced
	 * by ExtractGeometryBundle, PG index == the source section index that LODMaterialMap is
	 * indexed by.
	 */
	int32 ResolveBundleMaterialIndex(
		const FMetaHumanCrowdMeshGeometryBundle& Bundle,
		int32 LODIndex,
		int32 PGIndex);

	/**
	 * Write a content-addressable GUID into the mesh's FSkeletalMeshModel::SkeletalMeshModelGUID,
	 * so identical mesh state converges to the same DDC key, for the properties that the Crowd
	 * pipeline cares about. This shouldn't be used in a generic engine context.
	 * 
	 * Maximises DDC hits for redundant invalidations, reverts, and across machines arriving
	 * at the same state, while still forcing a miss whenever any hashed input changes.
	 *
	 * Must be called AFTER any edits to Materials/LODMaterialMap and BEFORE PostEditChange
	 * triggers the build, so that FSkeletalMeshRenderData::Cache computes the new key before
	 * the DDC fetch. No-op if Mesh->GetImportedModel() is null.
	 */
	void ApplyContentAddressableSkelMeshGuid(TNotNull<USkeletalMesh*> Mesh);

	/**
	 * RAII scope that wraps the engine's FScopedSkeletalMeshPostEditChange and, immediately
	 * before the wrapped scope destructs and triggers the mesh build, calls
	 * ApplyContentAddressableSkelMeshGuid on the mesh to ensure a deterministic
	 * SkeletalMeshModelGUID is set prior to the DDC key being computed.
	 */
	class FScopedSkeletalMeshChange
	{
	public:
		explicit FScopedSkeletalMeshChange(TNotNull<USkeletalMesh*> InMesh);
		~FScopedSkeletalMeshChange();

		FScopedSkeletalMeshChange(const FScopedSkeletalMeshChange&) = delete;
		FScopedSkeletalMeshChange& operator=(const FScopedSkeletalMeshChange&) = delete;
		FScopedSkeletalMeshChange(FScopedSkeletalMeshChange&&) = delete;
		FScopedSkeletalMeshChange& operator=(FScopedSkeletalMeshChange&&) = delete;

	private:
		TNotNull<USkeletalMesh*> Mesh;

		// Declared last so its destructor runs after our destructor body applies the GUID.
		// The engine scope's destructor is what actually triggers PostEditChange / the build
		// when the outermost scope exits.
		FScopedSkeletalMeshPostEditChange EngineScope;
	};

	/**
	 * Combine a body animation's bone tracks with a face animation's float curves into a single
	 * UAnimSequence on TargetSkeleton. Body bone tracks are copied verbatim (no rest-pose rebase);
	 * face float curves are layered on top. Bone tracks from FaceAnim and float curves from
	 * BodyAnim are ignored.
	 *
	 * Intended for pipelines where face motion is driven by curves (e.g. RigLogic) rather than
	 * explicit face bone tracks, so the output can be evaluated on a mesh whose post-process ABP
	 * consumes those curves.
	 *
	 * @param FaceAnim			Animation whose float curves should be copied.
	 * @param BodyAnim			Animation whose bone tracks should be copied (and used as length/rate).
	 * @param TargetSkeleton	The merged skeleton covering all bones.
	 * @param BoneCompressionSettingsOverride
	 *							Optional override for the output anim's BoneCompressionSettings.
	 *							If null, the engine's default animation-recorder preset is used.
	 * @param AnimAssetName		Name of the output UAnimSequence asset.
	 * @param Outer				Outer object for the new UAnimSequence.
	 * @return					The combined UAnimSequence, or nullptr on failure.
	 */
	UAnimSequence* CopyAnimationCurves(
		TNotNull<UAnimSequence*> FaceAnim,
		TNotNull<UAnimSequence*> BodyAnim,
		TNotNull<USkeleton*> TargetSkeleton,
		TObjectPtr<UAnimBoneCompressionSettings> BoneCompressionSettingsOverride,
		const FString& AnimAssetName,
		UObject* Outer);


};
