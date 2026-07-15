// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCollectionPipeline.h"
#include "MetaHumanCrowdTypes.h"

#include "Item/MetaHumanCrowdCharacterPipeline.h"
#include "Item/MetaHumanCrowdGroomPipeline.h"

#include "MetaHumanCrowdPipeline.generated.h"

#define UE_API METAHUMANCROWD_API

class UAnimSequenceTransformProviderData;
class UAnimSequence;
class UMaterialInterface;
class UMaterialInstanceConstant;

USTRUCT()
struct FMetaHumanCrowdFaceSlotLODs
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<int32> SourceLODs;
};

USTRUCT()
struct FMetaHumanCrowdCollectionHeadBuildOutput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<USkeletalMesh> ActorFaceMesh;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> InstancedFaceMesh;

	/** Pre-built ASTPD for the instanced face mesh (SkinnedAsset = InstancedFaceMesh, Sequences/Layers from BakedAnimRootProvider). */
	UPROPERTY()
	TObjectPtr<UAnimSequenceTransformProviderData> InstancedFaceMeshTransformProvider;

	UPROPERTY()
	TObjectPtr<UAnimSequenceTransformProviderData> BakedAnimRootProvider;

	UPROPERTY()
	FMetaHumanPaletteItemKey CompatibleBody;

	/**
	 * Per-slot list of source face-LOD indices the slot serves on the actor face mesh. Computed
	 * at build time so AssembleCollection can decide whether a {WardrobeSlot}AttributeMap write
	 * actually applies to a given face MID (the AttributeMap binds at face LODs >= MinBakedGroom-
	 * LOD; slots whose LOD coverage doesn't intersect that range should skip the write).
	 */
	UPROPERTY()
	TMap<FName, FMetaHumanCrowdFaceSlotLODs> ActorFaceSlotSourceLODs;

	/** See ActorFaceSlotSourceLODs; same meaning, instanced face mesh. */
	UPROPERTY()
	TMap<FName, FMetaHumanCrowdFaceSlotLODs> InstancedFaceSlotSourceLODs;

	/**
	 * Snapshot of the instanced face mesh's per-slot MIC after ApplySlotMaterialOverrides has
	 * reparented runtime-MID slots. Consumed by BuildFaceMICCombos as the parent for the
	 * generated combo MICs. Keys mirror FaceMaterialSlotsForRuntimeMID.
	 */
	UPROPERTY()
	TMap<FName, TObjectPtr<UMaterialInterface>> InstancedFaceSourceMICs;
};

/**
 * Per-(groom, head) face-side parameter binding emitted by the editor pipeline. At runtime the
 * collection's AssembleCollection / SetPostAssemblyParameters reads these and writes the values
 * onto the per-instance face MIDs (FMetaHumanCrowdAssemblyOutput::Actor/InstancedFaceMaterialOverrides),
 * giving runtime control over hair color and the {WardrobeSlot}AttributeMap binding for grooms
 * that use baked-groom textures on the face material.
 *
 * Each entry is a parallel of the groom's RuntimeMaterialParameters with MaterialParameter.Name
 * rewritten to "{WardrobeSlot}{OriginalName}" (e.g. "HairMelanin") and SlotNames pointing at the
 * face slots wrapped by FaceMaterialSlotsForRuntimeMID. The InstanceParameterName is unchanged
 * so a single user-facing slider drives both the groom's own MID and the face MID.
 */
USTRUCT()
struct FMetaHumanCrowdGroomFaceParameterBinding
{
	GENERATED_BODY()

public:
	/** Groom item path this binding belongs to. */
	UPROPERTY()
	FMetaHumanPaletteItemPath GroomItemPath;

	/** Head item this groom binds to. Used to find the right face MID at runtime. */
	UPROPERTY()
	FMetaHumanPaletteItemKey HeadItemKey;

	/**
	 * Pipeline slot the groom lives in (e.g. "Hair", "Beard"); used as a parameter-name prefix
	 * and to compute the {WardrobeSlot}AttributeMap parameter.
	 */
	UPROPERTY()
	FName PipelineSlotName;

	/** Material parameters with prefixed names targeting the face slots. */
	UPROPERTY()
	TArray<FMetaHumanMaterialParameter> Parameters;
};

/**
 * Single (pipeline slot, equipped-groom key) entry inside FMetaHumanCrowdFaceMICComboKey.
 */
USTRUCT()
struct FMetaHumanCrowdFaceMICComboSlot
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FName PipelineSlotName;

	UPROPERTY()
	FMetaHumanPaletteItemKey EquippedGroomKey;

	bool operator==(const FMetaHumanCrowdFaceMICComboSlot& Other) const
	{
		return PipelineSlotName == Other.PipelineSlotName && EquippedGroomKey == Other.EquippedGroomKey;
	}

	friend uint32 GetTypeHash(const FMetaHumanCrowdFaceMICComboSlot& In)
	{
		return HashCombine(GetTypeHash(In.PipelineSlotName), GetTypeHash(In.EquippedGroomKey));
	}
};

/**
 * Identifies a (head, equipped-grooms) combination that drives a unique face MIC set.
 *
 * EquippedGrooms is sorted by slot name for stable hashing and equality. A default-constructed
 * EquippedGroomKey means "no groom equipped in this slot".
 *
 * Built by the editor pipeline by enumerating across face-affecting slots, looked up at runtime
 * by AssembleCollection to share face MaterialInterface pointers across UMetaHumanInstances.
 */
USTRUCT()
struct FMetaHumanCrowdFaceMICComboKey
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FMetaHumanPaletteItemKey HeadKey;

	UPROPERTY()
	TArray<FMetaHumanCrowdFaceMICComboSlot> EquippedGrooms;

	bool operator==(const FMetaHumanCrowdFaceMICComboKey& Other) const
	{
		return HeadKey == Other.HeadKey && EquippedGrooms == Other.EquippedGrooms;
	}

	friend UE_API uint32 GetTypeHash(const FMetaHumanCrowdFaceMICComboKey& Key);
};

/**
 * Pre-baked face MICs for one (head, groom combo) pair. Actor and instanced variants are stored
 * separately because each has its own source MIC identity (per ApplySlotMaterialOverrides).
 */
USTRUCT()
struct FMetaHumanCrowdFaceMICSet
{
	GENERATED_BODY()

public:
	/**
	 * Per-face-slot shared MIC for the instanced (ISKM) variant, assigned directly at
	 * AssembleCollection time.
	 */
	UPROPERTY()
	TMap<FName, TObjectPtr<UMaterialInstanceConstant>> InstancedSlotToMIC;
};

/** Build output for the collection pipeline itself (not per-item). */
USTRUCT()
struct FMetaHumanCrowdCollectionBuildOutput
{
	GENERATED_BODY()

public:
	/** If true, body geometry has been merged onto clothing meshes during the collection build. */
	UPROPERTY()
	bool bBodyGeometryMergedOntoClothing = false;

	/**
	 * Merged animations (body bone tracks + face curves) shared across all characters.
	 * Keyed by FMetaHumanCrowdBakeAnimationData::Name from the animation config. Anim BPs
	 * read the equivalent map on FMetaHumanCrowdAssemblyOutput to drive character animation.
	 */
	UPROPERTY()
	TMap<FName, TObjectPtr<UAnimSequence>> SharedAnimBPAnimations;

	/**
	 * Face material slot names that the editor pipeline reparented during ApplyFaceMaterialOverrides.
	 * Runtime AssembleCollection wraps the face MIC at each of these slots in a UMaterialInstanceDynamic
	 * so per-MetaHuman parameter writes (hair color, etc.) can drive the face material.
	 */
	UPROPERTY()
	TArray<FName> FaceMaterialSlotsForRuntimeMID;

	/**
	 * Per-(groom, head) parameter bindings driving the face material at runtime. See
	 * FMetaHumanCrowdGroomFaceParameterBinding for the per-entry shape and the routing flow.
	 * Empty when no grooms target face slots in FaceMaterialSlotsForRuntimeMID.
	 */
	UPROPERTY()
	TArray<FMetaHumanCrowdGroomFaceParameterBinding> GroomFaceParameterBindings;

	/**
	 * Per face slot, the parameter-name -> custom-data-offset format map authored on the
	 * editor pipeline's FaceMaterialOverrides[i].InstanceParameterNameToCustomDataFormat.
	 *
	 * Captured at build time so the runtime AssembleCollection can write per-instance face
	 * colour parameters into the face ISKM's custom-data buffer without round-tripping back
	 * to the editor pipeline.
	 *
	 * Keys are face material slot names (matching FaceMaterialSlotsForRuntimeMID). Values are
	 * the same shape as FMetaHumanCrowdOutfitInstancedMaterial::InstanceParameterNameToCustomDataFormat
	 * so the existing MaterialUtils::SetInstanceParametersOnCustomData path can be reused.
	 */
	UPROPERTY()
	TMap<FName, FMetaHumanCrowdOutfitInstancedMaterial> FaceSlotCustomDataLayout;

	/**
	 * Pipeline slot names that participate in the face-MIC combo enumeration, sorted
	 * alphabetically. Recorded at build time so the runtime knows which slots to read from
	 * Params.SlotSelections when constructing the FaceMICsByCombo lookup key.
	 */
	UPROPERTY()
	TArray<FName> FaceAffectingPipelineSlots;

	/**
	 * Pre-baked face MICs keyed by (head, equipped-grooms) combo. Replaces the per-Assemble
	 * face MID creation: at runtime, AssembleCollection looks up the MIC set for the current
	 * selection and assigns it directly to {Actor,Instanced}FaceMaterialOverrides. The shared
	 * MaterialInterface pointer across UMetaHumanInstances with the same combo lets the Mass
	 * ISKM subsystem batch them into a single ISKMC.
	 *
	 * Populated by UMetaHumanCrowdEditorPipeline::BuildFaceMICCombos.
	 */
	UPROPERTY()
	TMap<FMetaHumanCrowdFaceMICComboKey, FMetaHumanCrowdFaceMICSet> FaceMICsByCombo;
};

/** Collection-level build output for a body item. */
USTRUCT()
struct FMetaHumanCrowdCollectionBodyBuildOutput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<USkeletalMesh> ActorBodyMesh;

	/** nullptr when body geometry has been merged onto clothing meshes. */
	UPROPERTY()
	TObjectPtr<USkeletalMesh> InstancedBodyMesh;

	/** Pre-built ASTPD for the instanced body mesh. nullptr when InstancedBodyMesh is nullptr. */
	UPROPERTY()
	TObjectPtr<UAnimSequenceTransformProviderData> InstancedBodyMeshTransformProvider;
};

/** Collection-level build output for an outfit item. */
USTRUCT()
struct FMetaHumanCrowdCollectionOutfitBuildOutput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, TObjectPtr<USkeletalMesh>> BodyToActorOutfitMeshMap;

	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, TObjectPtr<USkeletalMesh>> BodyToInstancedOutfitMeshMap;

	/** Pre-built ASTPDs for the instanced outfit meshes, keyed by the same body item key as BodyToInstancedOutfitMeshMap. */
	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, TObjectPtr<UAnimSequenceTransformProviderData>> BodyToInstancedOutfitTransformProviderMap;
};

/** Collection-level build output for a groom item. */
USTRUCT()
struct FMetaHumanCrowdCollectionGroomBuildOutput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, TObjectPtr<USkeletalMesh>> ItemToActorGroomMeshMap;

	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, TObjectPtr<USkeletalMesh>> ItemToInstancedGroomMeshMap;

	/** Pre-built ASTPDs for the instanced groom meshes, keyed by the same head item key as ItemToInstancedGroomMeshMap. */
	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, TObjectPtr<UAnimSequenceTransformProviderData>> ItemToInstancedGroomTransformProviderMap;

	/**
	 * Per-(groom, head) baked groom texture entries surfaced from the item-level
	 * FMetaHumanCrowdGroomBuildOutput::ItemToBakedGroomTexture. Promoted to the collection-level
	 * output so runtime AssembleCollection can resolve the texture for each equipped groom; the
	 * item-level groom build data is consumed and discarded during integration.
	 */
	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdGroomBakedTextureEntry> ItemToBakedGroomTexture;

	/**
	 * Pipeline slot the groom lives in (e.g. "Hair", "Beard"). Surfaced from the item-level
	 * FMetaHumanCrowdGroomBuildOutput so runtime AssembleItem can scope the PostAssemblyParameters
	 * bag's keys with the slot suffix.
	 */
	UPROPERTY()
	FName PipelineSlotName;
};

USTRUCT(BlueprintType)
struct FMetaHumanCrowdActorClothingAssemblyOutput
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TObjectPtr<class USkeletalMesh> OutfitMesh;

	/** MID overrides, keyed by material slot name. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TMap<FName, TObjectPtr<UMaterialInterface>> OverrideMaterials;
};

USTRUCT(BlueprintType)
struct FMetaHumanCrowdInstancedClothingAssemblyOutput
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TObjectPtr<class USkeletalMesh> OutfitMesh;

	/** Pre-built ASTPD for this outfit mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TObjectPtr<class UAnimSequenceTransformProviderData> TransformProvider;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TMap<FName, TObjectPtr<UMaterialInterface>> InstancedMaterialData;

	/**
	 * ISKM-wide per-instance custom data float buffer (single flat array shared across all
	 * material slots on this ISKM). See FMetaHumanCrowdOutfitAssemblyOutput::InstancedMeshCustomDataFloats
	 * for offset semantics.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TArray<float> InstancedMeshCustomDataFloats;
};

USTRUCT(BlueprintType)
struct FMetaHumanCrowdActorGroomAssemblyOutput
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TObjectPtr<USkeletalMesh> CardsMesh;

	/** MID overrides, keyed by material slot name. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TMap<FName, TObjectPtr<UMaterialInterface>> OverrideMaterials;
};

USTRUCT(BlueprintType)
struct FMetaHumanCrowdInstancedGroomAssemblyOutput
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TObjectPtr<USkeletalMesh> CardsMesh;

	/** Pre-built ASTPD for this groom cards mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TObjectPtr<class UAnimSequenceTransformProviderData> TransformProvider;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TMap<FName, TObjectPtr<UMaterialInterface>> InstancedMaterialData;

	/**
	 * ISKM-wide per-instance custom data float buffer (single flat array shared across all
	 * material slots on this ISKM). See FMetaHumanCrowdOutfitAssemblyOutput::InstancedMeshCustomDataFloats
	 * for offset semantics.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TArray<float> InstancedMeshCustomDataFloats;
};

USTRUCT(BlueprintType)
struct FMetaHumanCrowdAssemblyOutput
{
	GENERATED_BODY()

public:
	// -- Actor --

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Actor")
	TObjectPtr<class USkeletalMesh> ActorFaceMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Actor")
	TObjectPtr<class USkeletalMesh> ActorBodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Actor")
	TArray<FMetaHumanCrowdActorClothingAssemblyOutput> ActorClothing;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Actor")
	TArray<FMetaHumanCrowdActorGroomAssemblyOutput> ActorGrooms;

	// -- Instanced --

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Instanced")
	TObjectPtr<class USkeletalMesh> InstancedFaceMesh;

	/** Pre-built ASTPD for the instanced face mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Instanced")
	TObjectPtr<class UAnimSequenceTransformProviderData> InstancedFaceMeshTransformProvider;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Instanced")
	TObjectPtr<class USkeletalMesh> InstancedBodyMesh;

	/** Pre-built ASTPD for the instanced body mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Instanced")
	TObjectPtr<class UAnimSequenceTransformProviderData> InstancedBodyMeshTransformProvider;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Instanced")
	TArray<FMetaHumanCrowdInstancedClothingAssemblyOutput> InstancedClothing;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Instanced")
	TArray<FMetaHumanCrowdInstancedGroomAssemblyOutput> InstancedGrooms;

	// -- Shared --

	/** True if the body mesh needs to be shown, false if the body geometry has been merged onto the clothing meshes */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	bool bIsBodyMeshVisible = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TObjectPtr<class UAnimSequenceTransformProviderData> BakedAnimRootProvider;

	/**
	 * Merged animations (body bone tracks + face curves) keyed by animation name.
	 * Counterpart to BakedAnimRootProvider for non-ISKM playback: Anim BPs read this
	 * map to drive the character with a full animation graph rather than GPU ISKM sequences.
	 * Shared across every character in the collection. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TMap<FName, TObjectPtr<UAnimSequence>> AnimBPAnimations;

	/**
	 * Per-instance face MIDs for the actor face mesh, keyed by face material slot name.
	 * Created by AssembleCollection by wrapping each MIC named in
	 * FMetaHumanCrowdCollectionBuildOutput::FaceMaterialSlotsForRuntimeMID; lets per-MetaHuman
	 * parameter writes (hair color, etc.) drive the face material at runtime. Empty when no
	 * face slots were reparented at build time.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TMap<FName, TObjectPtr<UMaterialInterface>> ActorFaceMaterialOverrides;

	/** Per-instance face MIDs for the instanced face mesh. Same shape as ActorFaceMaterialOverrides. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TMap<FName, TObjectPtr<UMaterialInterface>> InstancedFaceMaterialOverrides;

	/**
	 * Per-instance custom-data float buffer for the instanced face ISKM. Single flat array
	 * shared across every face material slot; per-slot offsets defined by
	 * FMetaHumanCrowdCollectionBuildOutput::FaceSlotCustomDataLayout. Written by
	 * AssembleCollection from the equipped grooms' assembly parameter bags via
	 * FMetaHumanCrowdGroomFaceParameterBinding. Empty when no face slot has a custom-data layout.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TArray<float> InstancedFaceMeshCustomDataFloats;
};

UCLASS(MinimalAPI, Abstract, Blueprintable, EditInlineNew)
class UMetaHumanCrowdPipeline : public UMetaHumanCollectionPipeline
{
	GENERATED_BODY()

public:
	UMetaHumanCrowdPipeline();

#if WITH_EDITOR
	virtual void SetDefaultEditorPipeline() override;
	virtual const UMetaHumanCollectionEditorPipeline* GetEditorPipeline() const override;
	virtual UMetaHumanCollectionEditorPipeline* GetMutableEditorPipeline() override;
#endif

	virtual void AssembleCollection(const FAssembleCollectionParams& Params, const FOnAssemblyComplete& OnComplete) const override;

	virtual bool AreSlotSelectionsAllowed(
		TNotNull<const UMetaHumanCollection*> Collection,
		TArrayView<const FMetaHumanPipelineSlotSelection> SlotSelections,
		FText& OutDisallowedReason) const override;

	virtual TNotNull<const UMetaHumanCharacterPipelineSpecification*> GetSpecification() const override
	{
		return Specification;
	}

	virtual const UMetaHumanItemPipeline* GetFallbackItemPipelineForAssetType(const TSoftClassPtr<UObject>& InAssetClass) const override;

	virtual TSubclassOf<AActor> GetActorClass() const override;

	UE_API static const FName HeadSlotName;
	UE_API static const FName BodySlotName;
	UE_API static const FName OutfitsSlotName;
	UE_API static const FName TopGarmentSlotName;
	UE_API static const FName BottomGarmentSlotName;
	UE_API static const FName ShoesSlotName;

	virtual void SetPostAssemblyParameters(
		TNotNull<const UMetaHumanCollection*> Collection,
		const FMetaHumanPostAssemblyParameterOutputCollectionView& AllPostAssemblyParameters,
		const FMetaHumanPaletteItemPath& TargetItemPath,
		const FInstancedPropertyBag& ModifiedPostAssemblyParameters,
		FInstancedStruct& InOutCollectionAssemblyOutput) const override;

protected:
	/**
	 * Full sync pass: writes the AttributeMap texture and color parameters onto the per-instance
	 * face MIDs for every (groom, head) binding. Equipped grooms get their textures and colors
	 * applied; non-equipped grooms get their AttributeMap cleared.
	 */
	void ApplyGroomFaceParameterBindings(
		const FAssembleCollectionParams& Params,
		const FMetaHumanPaletteItemKey& HeadItemKey,
		const FMetaHumanCrowdCollectionBuildOutput& CollectionBuildOutput,
		const FMetaHumanCrowdCollectionHeadBuildOutput& HeadBuildOutput,
		FMetaHumanCrowdAssemblyOutput& AssemblyOutput) const;

	/**
	 * Parameter-only update: applies color parameters from the given property bag onto the face
	 * MIDs for a single groom binding. AttributeMap textures are not touched (equip state is
	 * unchanged from the last AssembleCollection).
	 *
	 * Called from SetPostAssemblyParameters when the user edits a groom's parameters.
	 */
	void ApplyGroomFaceParametersForBag(
		const FMetaHumanCrowdCollectionBuildOutput& CollectionBuildOutput,
		const FMetaHumanPaletteItemPath& GroomItemPath,
		const FInstancedPropertyBag& ParameterBag,
		FMetaHumanCrowdAssemblyOutput& AssemblyOutput) const;

	virtual FInstancedStruct GetItemAssemblyOutputValue(
		TNotNull<const UMetaHumanCollection*> Collection,
		const FMetaHumanPostAssemblyParameterOutputCollectionView& AllPostAssemblyParameters,
		const FMetaHumanPaletteItemPath& TargetItemPath,
		const FInstancedPropertyBag& ModifiedPostAssemblyParameters,
		const FInstancedStruct& InCollectionAssemblyOutput) const override;
	
	virtual void SetItemAssemblyOutputValue(
		TNotNull<const UMetaHumanCollection*> Collection,
		const FMetaHumanPostAssemblyParameterOutputCollectionView& AllPostAssemblyParameters,
		const FMetaHumanPaletteItemPath& TargetItemPath,
		const FInstancedPropertyBag& ModifiedPostAssemblyParameters,
		FInstancedStruct&& ItemAssemblyOutput,
		FInstancedStruct& InOutCollectionAssemblyOutput) const override;

private:
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, NoClear, Instanced, Category = "Character", meta = (FullyExpand, AllowedClasses = "/Script/MetaHumanCrowdEditor.MetaHumanCrowdEditorPipeline"))
	TObjectPtr<UMetaHumanCollectionEditorPipeline> EditorPipeline;
#endif

	/** The specification that this pipeline implements */
	UPROPERTY(Transient)
	TObjectPtr<UMetaHumanCharacterPipelineSpecification> Specification;

	UPROPERTY(EditAnywhere, Category = "Character", meta = (MustImplement = "/Script/MetaHumanCharacterPalette.MetaHumanCharacterActorInterface"))
	TSubclassOf<AActor> ActorClass;
};

#undef UE_API
