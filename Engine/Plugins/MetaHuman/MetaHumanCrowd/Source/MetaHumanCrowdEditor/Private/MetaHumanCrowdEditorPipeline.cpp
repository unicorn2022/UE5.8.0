// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCrowdEditorPipeline.h"
#include "MetaHumanCrowdBodyMerge.h"
#include "MetaHumanCrowdMaterialEditorUtils.h"
#include "MetaHumanCrowdSkeletonPickerDialog.h"
#include "MetaHumanCrowdTypes.h"
#include "MetaHumanPipelineBuiltDataCollection.h"

#include "Item/MetaHumanCrowdCharacterEditorPipeline.h"
#include "Item/MetaHumanCrowdCharacterPipeline.h"
#include "Item/MetaHumanCrowdGroomEditorPipeline.h"
#include "Item/MetaHumanCrowdGroomPipeline.h"
#include "Item/MetaHumanCrowdOutfitEditorPipeline.h"
#include "Item/MetaHumanCrowdOutfitPipeline.h"
#include "Item/MetaHumanCrowdSkeletalClothingEditorPipeline.h"
#include "Item/MetaHumanCrowdSkeletalClothingPipeline.h"
#include "Item/MetaHumanSkeletalMeshPipeline.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorActor.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCrowdEditorLog.h"
#include "MetaHumanCrowdEditorPipelineLocals.h"
#include "MetaHumanCrowdEditorUtilities.h"
#include "MetaHumanCrowdPipeline.h"
#include "MetaHumanItemEditorPipeline.h"
#include "MetaHumanGeometryRemoval.h"
#include "MetaHumanStructHost.h"
#include "MetaHumanCrowdAnimationConfig.h"
#include "MetaHumanCharacterPipelineSpecification.h"
#include "Item/MetaHumanGroomPipeline.h"
#include "Item/MetaHumanOutfitPipeline.h"
#include "MetaHumanWardrobeItem.h"
#include "Subsystem/MetaHumanCharacterBuild.h"
#include "Subsystem/MetaHumanCharacterSkinMaterials.h"

#include "Algo/Transform.h"
#include "Engine/SkeletalMesh.h"
#include "ChaosOutfitAsset/OutfitAsset.h"
#include "GroomBindingAsset.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Logging/LogMacros.h"
#include "Misc/App.h"
#include "Misc/NotNull.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceTransformProviderData.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"
#include "AnimationUtils.h"

namespace UE::MetaHuman::CrowdEditorPipelinePrivate
{

/**
 * Composite key used by the combo-MIC baker to look up per-(groom, head) baked-texture entries.
 * File-scoped (rather than function-local) so a friend GetTypeHash can be defined alongside it.
 */
struct FGroomBakedKey
{
	FMetaHumanPaletteItemKey HeadKey;
	FMetaHumanPaletteItemKey GroomKey;

	bool operator==(const FGroomBakedKey& Other) const
	{
		return HeadKey == Other.HeadKey && GroomKey == Other.GroomKey;
	}
};

inline uint32 GetTypeHash(const FGroomBakedKey& In)
{
	return HashCombine(GetTypeHash(In.HeadKey), GetTypeHash(In.GroomKey));
}

/**
 * Content key for face combo-MIC dedup: parent material pointer + sorted (slot -> texture) pairs.
 * Two combos that resolve to the same content key share a single baked MIC.
 * File-scoped so GetTypeHash can be defined alongside it (friend definitions are not
 * permitted in function-local classes).
 */
struct FMICContentKey
{
	UMaterialInterface* Parent = nullptr;
	TArray<TTuple<FName, UTexture*>> SlotTextures; // sorted by FName for determinism

	bool operator==(const FMICContentKey& Other) const
	{
		return Parent == Other.Parent && SlotTextures == Other.SlotTextures;
	}
};

inline uint32 GetTypeHash(const FMICContentKey& Key)
{
	uint32 Hash = GetTypeHash(Key.Parent);
	for (const TTuple<FName, UTexture*>& Pair : Key.SlotTextures)
	{
		Hash = HashCombine(Hash, GetTypeHash(Pair.Key));
		Hash = HashCombine(Hash, GetTypeHash(Pair.Value));
	}
	return Hash;
}

/**
 * Creates a per-mesh ASTPD by copying Sequences and Layers from a root provider
 * and binding SkinnedAsset to the given skeletal mesh.
 *
 * This is the build-time equivalent of the runtime clone logic that previously
 * lived in MetaHumanMassRepresentationSubsystem::GetSequence(). Creating these at
 * build time allows the ASTPD's derived data cache (per-sequence bounding boxes)
 * to go through the cook pipeline.
 */
static UAnimSequenceTransformProviderData* CreatePerMeshTransformProvider(
	UAnimSequenceTransformProviderData* RootProvider,
	USkeletalMesh* TargetMesh,
	const FString& AssetName,
	UObject* Outer)
{
	if (!RootProvider || !TargetMesh)
	{
		return nullptr;
	}

	UAnimSequenceTransformProviderData* PerMeshProvider = NewObject<UAnimSequenceTransformProviderData>(Outer, *AssetName, RF_Public);

	// Set SkinnedAsset to the target mesh
	FObjectPropertyBase* SkinnedAssetProperty = CastField<FObjectPropertyBase>(
		UAnimSequenceTransformProviderData::StaticClass()->FindPropertyByName(TEXT("SkinnedAsset")));
	check(SkinnedAssetProperty);
	SkinnedAssetProperty->SetObjectPropertyValue_InContainer(PerMeshProvider, TargetMesh);

	// Copy Sequences from root
	FProperty* SequencesProperty = UAnimSequenceTransformProviderData::StaticClass()->FindPropertyByName(TEXT("Sequences"));
	check(SequencesProperty);
	SequencesProperty->CopyCompleteValue_InContainer(PerMeshProvider, RootProvider);

	// Copy Layers from root
	FProperty* LayersProperty = UAnimSequenceTransformProviderData::StaticClass()->FindPropertyByName(TEXT("Layers"));
	check(LayersProperty);
	LayersProperty->CopyCompleteValue_InContainer(PerMeshProvider, RootProvider);

	return PerMeshProvider;
}

static const UMetaHumanItemPipeline* GetOrCreateDefaultItemPipeline(FMetaHumanCrowdEditorPipelineLocals& Locals, TSubclassOf<UMetaHumanItemPipeline> ExpectedClass)
{
	// Store default pipelines on Locals to prevent them from being GCed during the build.
	const FName CacheKey = ExpectedClass ? ExpectedClass->GetFName() : NAME_None;
	TObjectPtr<const UMetaHumanItemPipeline>& DefaultPipeline = Locals.DefaultPipelinesByClass.FindOrAdd(CacheKey);
	if (DefaultPipeline)
	{
		check(DefaultPipeline->GetClass() == ExpectedClass);

		return DefaultPipeline;
	}

	UMetaHumanItemPipeline* NewPipeline = NewObject<UMetaHumanItemPipeline>(GetTransientPackageAsObject(), ExpectedClass, NAME_None, RF_Transient);
	// We can't just return ItemPipelineClass's default object, because the editor pipeline has to 
	// be created as a subobject of it, and because the two classes usually come from different 
	// modules, having the CDOs intertwined causes issues.
	NewPipeline->SetDefaultEditorPipeline();

	// Assign back to the map
	DefaultPipeline = NewPipeline;

	return DefaultPipeline;
}

static bool IsGroomSlot(const UMetaHumanCharacterPipelineSpecification* Spec, FName SlotName)
{
	const TOptional<FName> RealSlotName = Spec->ResolveRealSlotName(SlotName);
	if (!RealSlotName.IsSet())
	{
		return false;
	}

	return RealSlotName == CharacterPipelineSlots::Hair
		|| RealSlotName == CharacterPipelineSlots::Eyebrows
		|| RealSlotName == CharacterPipelineSlots::Mustache
		|| RealSlotName == CharacterPipelineSlots::Beard;
}

static bool IsOutfitSlot(const UMetaHumanCharacterPipelineSpecification* Spec, FName SlotName)
{
	const TOptional<FName> RealSlotName = Spec->ResolveRealSlotName(SlotName);
	if (!RealSlotName.IsSet())
	{
		return false;
	}

	return RealSlotName == UMetaHumanCrowdPipeline::OutfitsSlotName;
}


} // namespace UE::MetaHuman::CrowdEditorPipelinePrivate

UMetaHumanCrowdEditorPipeline::UMetaHumanCrowdEditorPipeline()
{
	{
		ActorMeshMinLOD.Default = 0;
		ActorMeshMinLOD.PerPlatform.Add(TEXT("Mobile"), 1);

		ActorMeshQualityLevelMinLOD.Default = 0;
		ActorMeshQualityLevelMinLOD.PerQuality.Add(static_cast<int32>(EPerQualityLevels::Low), 1);

		InstancedMeshMinLOD.Default = 0;
		InstancedMeshMinLOD.PerPlatform.Add(TEXT("Mobile"), 1);

		InstancedMeshQualityLevelMinLOD.Default = 0;
		InstancedMeshQualityLevelMinLOD.PerQuality.Add(static_cast<int32>(EPerQualityLevels::Low), 1);

		ActorFaceLODs.Add({ .SourceLOD = 2 });
		ActorFaceLODs.Add({ .SourceLOD = 3 });
		ActorBodyLODs.Add({ .SourceLOD = 1 });

		InstancedFaceLODs.Add({ .SourceLOD = 4, .ScreenSize = 1.0f });
		InstancedFaceLODs.Add({ .SourceLOD = 6, .ScreenSize = 0.3f });
		InstancedBodyLODs.Add({ .SourceLOD = 2, .ScreenSize = 1.0f });
		InstancedBodyLODs.Add({ .SourceLOD = 3, .ScreenSize = 0.3f });
	}

	GroomPipelineClass = UMetaHumanCrowdGroomPipeline::StaticClass();
	OutfitPipelineClass = UMetaHumanCrowdOutfitPipeline::StaticClass();
	SkeletalClothingPipelineClass = UMetaHumanCrowdSkeletalClothingPipeline::StaticClass();

	Specification = CreateDefaultSubobject<UMetaHumanCharacterEditorPipelineSpecification>("Specification");

	{
		FMetaHumanCharacterPipelineSlotEditorData& Slot = Specification->SlotEditorData.FindOrAdd(UMetaHumanCrowdPipeline::HeadSlotName);
		Slot.BuildInputStruct = FMetaHumanCrowdCharacterBuildInput::StaticStruct();
		Slot.SupportedPrincipalAssetTypes.Add(UMetaHumanCharacter::StaticClass());
	}

	{
		FMetaHumanCharacterPipelineSlotEditorData& Slot = Specification->SlotEditorData.FindOrAdd(UMetaHumanCrowdPipeline::BodySlotName);
		Slot.BuildInputStruct = FMetaHumanCrowdCharacterBuildInput::StaticStruct();
		Slot.SupportedPrincipalAssetTypes.Add(UMetaHumanCharacter::StaticClass());
	}

	{
		FMetaHumanCharacterPipelineSlotEditorData& Slot = Specification->SlotEditorData.FindOrAdd(UMetaHumanCrowdPipeline::OutfitsSlotName);
		Slot.BuildInputStruct = FMetaHumanCrowdOutfitBuildInput::StaticStruct();
		Slot.SupportedPrincipalAssetTypes.Add(UChaosOutfitAsset::StaticClass());
		Slot.SupportedPrincipalAssetTypes.Add(USkeletalMesh::StaticClass());
	}

	{
		FMetaHumanCharacterPipelineSlotEditorData& Slot = Specification->SlotEditorData.FindOrAdd(UMetaHumanCrowdPipeline::TopGarmentSlotName);
		Slot.SupportedPrincipalAssetTypes.Add(UChaosOutfitAsset::StaticClass());
		Slot.SupportedPrincipalAssetTypes.Add(USkeletalMesh::StaticClass());
	}

	{
		FMetaHumanCharacterPipelineSlotEditorData& Slot = Specification->SlotEditorData.FindOrAdd(UMetaHumanCrowdPipeline::BottomGarmentSlotName);
		Slot.SupportedPrincipalAssetTypes.Add(UChaosOutfitAsset::StaticClass());
		Slot.SupportedPrincipalAssetTypes.Add(USkeletalMesh::StaticClass());
	}

	{
		FMetaHumanCharacterPipelineSlotEditorData& Slot = Specification->SlotEditorData.FindOrAdd(UMetaHumanCrowdPipeline::ShoesSlotName);
		Slot.SupportedPrincipalAssetTypes.Add(UChaosOutfitAsset::StaticClass());
		Slot.SupportedPrincipalAssetTypes.Add(USkeletalMesh::StaticClass());
	}

	{
		FMetaHumanCharacterPipelineSlotEditorData& Slot = Specification->SlotEditorData.FindOrAdd(UE::MetaHuman::CharacterPipelineSlots::Hair);
		Slot.SupportedPrincipalAssetTypes.Add(UGroomBindingAsset::StaticClass());
	}

	{
		FMetaHumanCharacterPipelineSlotEditorData& Slot = Specification->SlotEditorData.FindOrAdd(UE::MetaHuman::CharacterPipelineSlots::Eyebrows);
		Slot.SupportedPrincipalAssetTypes.Add(UGroomBindingAsset::StaticClass());
	}

	{
		FMetaHumanCharacterPipelineSlotEditorData& Slot = Specification->SlotEditorData.FindOrAdd(UE::MetaHuman::CharacterPipelineSlots::Beard);
		Slot.SupportedPrincipalAssetTypes.Add(UGroomBindingAsset::StaticClass());
	}

	{
		FMetaHumanCharacterPipelineSlotEditorData& Slot = Specification->SlotEditorData.FindOrAdd(UE::MetaHuman::CharacterPipelineSlots::Mustache);
		Slot.SupportedPrincipalAssetTypes.Add(UGroomBindingAsset::StaticClass());
	}
}

void UMetaHumanCrowdEditorPipeline::BuildCollection(const FBuildCollectionParams& Params, const FOnBuildComplete& OnComplete) const
{
	// Make BuiltData visible to GC, in case there's a GC during the build (some items force a GC)
	TStrongObjectPtr<UMetaHumanStructHost> BuiltDataHost = TStrongObjectPtr(NewObject<UMetaHumanStructHost>());
	FMetaHumanCollectionBuiltData* BuiltData = &BuiltDataHost->Struct.InitializeAs<FMetaHumanCollectionBuiltData>();

	TStrongObjectPtr<UMetaHumanStructHost> LocalsHost = TStrongObjectPtr(NewObject<UMetaHumanStructHost>());
	FMetaHumanCrowdEditorPipelineLocals& Locals = LocalsHost->Struct.InitializeAs<FMetaHumanCrowdEditorPipelineLocals>();

	// The Collection's own build output
	//
	// The reference is invalidated when item built data is added, so don't keep it
	{
		FMetaHumanPipelineBuiltData& CollectionBuildData = BuiltData->PaletteBuiltData.ItemBuiltData.Edit().Add(FMetaHumanPaletteItemPath::Collection);
		CollectionBuildData.BuildOutput.InitializeAs<FMetaHumanCrowdCollectionBuildOutput>();
	}
	// Use this accessor to get a fresh reference to the Collection's build output
	auto GetCollectionBuildOutput = [BuiltData]() -> FMetaHumanCrowdCollectionBuildOutput&
	{
		return BuiltData->PaletteBuiltData.ItemBuiltData.MutableView()[FMetaHumanPaletteItemPath::Collection].BuildOutput.GetMutable<FMetaHumanCrowdCollectionBuildOutput>();
	};

	if (ActorFaceLODs.Num() == 0
		|| ActorBodyLODs.Num() == 0
		|| InstancedFaceLODs.Num() == 0
		|| InstancedBodyLODs.Num() == 0)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Must have at least one LOD for actors and instances, faces and bodies");

		OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed, nullptr);
		return;
	}

	// Extract the LOD settings into flat arrays, which are used at various parts of the build process
	TArray<int32> ActorFaceSourceLODs;
	Algo::Transform(ActorFaceLODs, ActorFaceSourceLODs, &FMetaHumanCrowdActorLODSettings::SourceLOD);

	TArray<int32> ActorBodySourceLODs;
	Algo::Transform(ActorBodyLODs, ActorBodySourceLODs, &FMetaHumanCrowdActorLODSettings::SourceLOD);

	TArray<int32> InstancedFaceSourceLODs;
	Algo::Transform(InstancedFaceLODs, InstancedFaceSourceLODs, &FMetaHumanCrowdInstancedLODSettings::SourceLOD);

	TArray<int32> InstancedBodySourceLODs;
	Algo::Transform(InstancedBodyLODs, InstancedBodySourceLODs, &FMetaHumanCrowdInstancedLODSettings::SourceLOD);

	TArray<FPerPlatformFloat> InstancedFaceScreenSizes;
	Algo::Transform(InstancedFaceLODs, InstancedFaceScreenSizes, &FMetaHumanCrowdInstancedLODSettings::ScreenSize);

	TArray<FPerPlatformFloat> InstancedBodyScreenSizes;
	Algo::Transform(InstancedBodyLODs, InstancedBodyScreenSizes, &FMetaHumanCrowdInstancedLODSettings::ScreenSize);

	if (!TargetSkeleton)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "TargetSkeleton is not set");

		OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed, nullptr);
		return;
	}

	// Key is head item key, value is body item key of the head's CompatibleBody
	TMap<FMetaHumanPaletteItemKey, FMetaHumanPaletteItemKey> CompatibleBodyMap;

	// Heads
	for (const FMetaHumanCharacterPaletteItem& Item : Params.Collection->GetItems())
	{
		if (Item.SlotName != UMetaHumanCrowdPipeline::HeadSlotName)
		{
			continue;
		}

		UObject* PrincipalAsset = Item.LoadPrincipalAssetSynchronous();
		const FMetaHumanPaletteItemKey ItemKey(Item.GetItemKey());
		const FMetaHumanPaletteItemPath ItemPath(ItemKey);

		if (Params.SortedItemsToExclude.Contains(ItemPath))
		{
			continue;
		}

		if (!PrincipalAsset || !PrincipalAsset->IsA<UMetaHumanCharacter>())
		{
			continue;
		}

		const UMetaHumanItemPipeline* ItemPipeline = nullptr;
		static_cast<void>(Params.Collection->TryResolveItemPipeline(ItemPath, ItemPipeline));

		if (!ItemPipeline)
		{
			ItemPipeline = UE::MetaHuman::CrowdEditorPipelinePrivate::GetOrCreateDefaultItemPipeline(
				Locals, UMetaHumanCrowdCharacterPipeline::StaticClass());
		}

		const UMetaHumanItemEditorPipeline* ItemEditorPipeline = ItemPipeline->GetEditorPipeline();
		if (!ItemEditorPipeline)
		{
			// Can't build this item without an editor pipeline

			// TODO: Log
			continue;
		}

		FInstancedStruct ItemBuildInput;
		FMetaHumanCrowdCharacterBuildInput& CharacterBuildInput = ItemBuildInput.InitializeAs<FMetaHumanCrowdCharacterBuildInput>();
		// Face needs to be modifiable because we strip LODs etc
		CharacterBuildInput.FaceMeshRequirement = EMetaHumanCrowdCharacterMeshRequirement::Modifiable;
		CharacterBuildInput.BodyMeshRequirement = EMetaHumanCrowdCharacterMeshRequirement::NotNeeded;
		CharacterBuildInput.MergedHeadAndBodyMeshRequirement = EMetaHumanCrowdCharacterMeshRequirement::NotNeeded;
		CharacterBuildInput.bGenerateBodyMeasurements = false;

		FMetaHumanPaletteBuiltData ItemBuiltData;

		const UMetaHumanItemEditorPipeline::FBuildItemParams ItemParams
		{
			.ItemPath = ItemPath,
			.WardrobeItem = Item.WardrobeItem,
			.Quality = Params.Collection->GetQuality(),
			.OuterForGeneratedObjects = Params.OuterForGeneratedAssets,
			.BuildInput = MoveTemp(ItemBuildInput),
			.BuildCacheGuid = Params.Collection->BuildCacheGuid
		};

		ItemEditorPipeline->BuildItemSynchronous(ItemParams, ItemBuiltData);

		if (ItemBuiltData.ContainsOnlyValidBuildOutputForItem(ItemPath))
		{
			const FMetaHumanCrowdCharacterBuildOutput* CharacterOutput = ItemBuiltData.ItemBuiltData.View()[ItemPath].BuildOutput.GetPtr<FMetaHumanCrowdCharacterBuildOutput>();
			if (CharacterOutput && CharacterOutput->FaceMesh)
			{
				FMetaHumanCrowdGroomFitTarget& GroomFitTarget = Locals.HeadGroomFitTargets.Add(ItemKey);
				GroomFitTarget.OptionalCharacter = Cast<UMetaHumanCharacter>(PrincipalAsset);
				GroomFitTarget.TargetMesh = CharacterOutput->FaceMesh;

				// Hold the raw character-pipeline build output until the collection-level head build
				// output is synthesised and integrated at the end of the build (Phase 6).
				Locals.HeadCharacterBuildOutputs.Add(ItemKey, *CharacterOutput);

				// Add the head's compatible body to CompatibleBodyMap, if any was specified
				if (CharacterOutput->CompatibleBody)
				{
					const FMetaHumanCharacterPaletteItem* BodyItem = Params.Collection->GetItems().FindByPredicate(
						[Body = CharacterOutput->CompatibleBody](const FMetaHumanCharacterPaletteItem& Element)
						{
							return Element.SlotName == UMetaHumanCrowdPipeline::BodySlotName
								&& Element.WardrobeItem->PrincipalAsset == Body;
						});

					if (BodyItem)
					{
						CompatibleBodyMap.Add(ItemKey, BodyItem->GetItemKey());
					}
				}
			}
		}
	}

	// Bodies
	for (const FMetaHumanCharacterPaletteItem& Item : Params.Collection->GetItems())
	{
		if (Item.SlotName != UMetaHumanCrowdPipeline::BodySlotName)
		{
			continue;
		}

		UObject* PrincipalAsset = Item.LoadPrincipalAssetSynchronous();
		const FMetaHumanPaletteItemKey ItemKey(Item.GetItemKey());
		const FMetaHumanPaletteItemPath ItemPath(ItemKey);

		if (Params.SortedItemsToExclude.Contains(ItemPath))
		{
			continue;
		}

		if (!PrincipalAsset || !PrincipalAsset->IsA<UMetaHumanCharacter>())
		{
			continue;
		}

		const UMetaHumanItemPipeline* ItemPipeline = nullptr;
		static_cast<void>(Params.Collection->TryResolveItemPipeline(ItemPath, ItemPipeline));

		if (!ItemPipeline)
		{
			ItemPipeline = UE::MetaHuman::CrowdEditorPipelinePrivate::GetOrCreateDefaultItemPipeline(
				Locals, UMetaHumanCrowdCharacterPipeline::StaticClass());
		}

		const UMetaHumanItemEditorPipeline* ItemEditorPipeline = ItemPipeline->GetEditorPipeline();
		if (!ItemEditorPipeline)
		{
			// Can't build this item without an editor pipeline

			// TODO: Log
			continue;
		}

		FInstancedStruct ItemBuildInput;
		FMetaHumanCrowdCharacterBuildInput& CharacterBuildInput = ItemBuildInput.InitializeAs<FMetaHumanCrowdCharacterBuildInput>();
		CharacterBuildInput.FaceMeshRequirement = EMetaHumanCrowdCharacterMeshRequirement::NotNeeded;
		CharacterBuildInput.BodyMeshRequirement = EMetaHumanCrowdCharacterMeshRequirement::Modifiable;
		// The merged mesh is only used for outfit fitting, so it can be read only
		CharacterBuildInput.MergedHeadAndBodyMeshRequirement = EMetaHumanCrowdCharacterMeshRequirement::ReadOnly;
		CharacterBuildInput.bGenerateBodyMeasurements = true;

		FMetaHumanPaletteBuiltData ItemBuiltData;

		const UMetaHumanItemEditorPipeline::FBuildItemParams ItemParams
		{
			.ItemPath = ItemPath,
			.WardrobeItem = Item.WardrobeItem,
			.Quality = Params.Collection->GetQuality(),
			.OuterForGeneratedObjects = Params.OuterForGeneratedAssets,
			.BuildInput = MoveTemp(ItemBuildInput),
			.BuildCacheGuid = Params.Collection->BuildCacheGuid
		};

		ItemEditorPipeline->BuildItemSynchronous(ItemParams, ItemBuiltData);

		if (ItemBuiltData.ContainsOnlyValidBuildOutputForItem(ItemPath))
		{
			if (const FMetaHumanCrowdCharacterBuildOutput* CharacterOutput = ItemBuiltData.ItemBuiltData.View()[ItemPath].BuildOutput.GetPtr<FMetaHumanCrowdCharacterBuildOutput>())
			{
				if (CharacterOutput->MergedHeadAndBodyMesh)
				{
					const FMetaHumanCrowdOutfitFitTarget FitTarget
					{
						.BodyCharacter = Cast<UMetaHumanCharacter>(PrincipalAsset),
						.MergedHeadAndBodyMesh = CharacterOutput->MergedHeadAndBodyMesh
					};

					Locals.OutfitFitTargets.Add(ItemKey, FitTarget);
				}

				// Hold the raw character-pipeline build output until the collection-level body build
				// output is synthesised and integrated at the end of the build (Phase 6).
				Locals.BodyCharacterBuildOutputs.Add(ItemKey, *CharacterOutput);
			}
		}
	}

	// Outfits and hair
	for (const FMetaHumanCharacterPaletteItem& Item : Params.Collection->GetItems())
	{
		UObject* PrincipalAsset = Item.LoadPrincipalAssetSynchronous();
		const FMetaHumanPaletteItemKey ItemKey(Item.GetItemKey());
		const FMetaHumanPaletteItemPath ItemPath(ItemKey);

		if (Params.SortedItemsToExclude.Contains(ItemPath))
		{
			continue;
		}

		const UMetaHumanItemPipeline* ItemPipeline = nullptr;
		static_cast<void>(Params.Collection->TryResolveItemPipeline(ItemPath, ItemPipeline));

		const TOptional<FName> RealSlotName = GetRuntimePipeline()->GetSpecification()->ResolveRealSlotName(Item.SlotName);
		if (!RealSlotName.IsSet())
		{
			continue;
		}

		if (RealSlotName == UMetaHumanCrowdPipeline::OutfitsSlotName)
		{
			if (!ItemPipeline)
			{
				const TSubclassOf<UMetaHumanItemPipeline> DefaultPipelineClass =
					Cast<USkeletalMesh>(PrincipalAsset)
						? TSubclassOf<UMetaHumanItemPipeline>(UMetaHumanCrowdSkeletalClothingPipeline::StaticClass())
						: TSubclassOf<UMetaHumanItemPipeline>(UMetaHumanCrowdOutfitPipeline::StaticClass());

				ItemPipeline = UE::MetaHuman::CrowdEditorPipelinePrivate::GetOrCreateDefaultItemPipeline(
					Locals, DefaultPipelineClass);
			}

			const UMetaHumanItemEditorPipeline* ItemEditorPipeline = ItemPipeline->GetEditorPipeline();
			if (!ItemEditorPipeline)
			{
				// Can't build this item without an editor pipeline

				// TODO: Log
				continue;
			}

			// TODO: There should be more checking here to make sure the item pipeline is 
			// compatible with this slot. This currently involves a lot of boilerplate, which
			// I don't want to add here. Once the boilerplate has been refactored to a nice 
			// helper function, we can call that from here to do the necessary checks.

			FInstancedStruct ItemBuildInput;
			FMetaHumanCrowdOutfitBuildInput& OutfitBuildInput = ItemBuildInput.InitializeAs<FMetaHumanCrowdOutfitBuildInput>();
			OutfitBuildInput.FitTargets = Locals.OutfitFitTargets;
			OutfitBuildInput.OutfitResizeDataflowAsset = OutfitResizeDataflowAsset;

			FMetaHumanPaletteBuiltData ItemBuiltData;

			const UMetaHumanItemEditorPipeline::FBuildItemParams ItemParams
			{
				.ItemPath = ItemPath,
				.WardrobeItem = Item.WardrobeItem,
				.Quality = Params.Collection->GetQuality(),
				.OuterForGeneratedObjects = Params.OuterForGeneratedAssets,
				.BuildInput = MoveTemp(ItemBuildInput),
				.BuildCacheGuid = Params.Collection->BuildCacheGuid
			};

			ItemEditorPipeline->BuildItemSynchronous(ItemParams, ItemBuiltData);

			if (ItemBuiltData.ContainsOnlyValidBuildOutputForItem(ItemPath))
			{
				// Defer integration until the item build output is transformed into the
				// collection-level outfit build output in Phase 3c+4c.
				FMetaHumanCrowdDeferredItemBuild& Deferred = Locals.DeferredOutfitBuilds.Add(ItemPath);
				Deferred.SlotName = Item.SlotName;
				Deferred.BuiltData = MoveTemp(ItemBuiltData);
			}
		}
		else if (RealSlotName == UE::MetaHuman::CharacterPipelineSlots::Hair
			|| RealSlotName == UE::MetaHuman::CharacterPipelineSlots::Eyebrows
			|| RealSlotName == UE::MetaHuman::CharacterPipelineSlots::Mustache
			|| RealSlotName == UE::MetaHuman::CharacterPipelineSlots::Beard)
		{
			if (!ItemPipeline)
			{
				ItemPipeline = UE::MetaHuman::CrowdEditorPipelinePrivate::GetOrCreateDefaultItemPipeline(
					Locals, UMetaHumanCrowdGroomPipeline::StaticClass());
			}

			const UMetaHumanItemEditorPipeline* ItemEditorPipeline = ItemPipeline->GetEditorPipeline();
			if (!ItemEditorPipeline)
			{
				// Can't build this item without an editor pipeline

				// TODO: Log
				continue;
			}

			// TODO: There should be more checking here to make sure the item pipeline is 
			// compatible with this slot. This currently involves a lot of boilerplate, which
			// I don't want to add here. Once the boilerplate has been refactored to a nice 
			// helper function, we can call that from here to do the necessary checks.

			FInstancedStruct ItemBuildInput;
			FMetaHumanCrowdGroomBuildInput& GroomBuildInput = ItemBuildInput.InitializeAs<FMetaHumanCrowdGroomBuildInput>();
			GroomBuildInput.FitTargets = Locals.HeadGroomFitTargets;
			GroomBuildInput.ActorFaceLODs = ActorFaceSourceLODs;
			GroomBuildInput.InstancedFaceLODs = InstancedFaceSourceLODs;
			GroomBuildInput.PipelineSlotName = *RealSlotName;

			FMetaHumanPaletteBuiltData ItemBuiltData;

			const UMetaHumanItemEditorPipeline::FBuildItemParams ItemParams
			{
				.ItemPath = ItemPath,
				.WardrobeItem = Item.WardrobeItem,
				.Quality = Params.Collection->GetQuality(),
				.OuterForGeneratedObjects = Params.OuterForGeneratedAssets,
				.BuildInput = MoveTemp(ItemBuildInput),
				.BuildCacheGuid = Params.Collection->BuildCacheGuid
			};

			ItemEditorPipeline->BuildItemSynchronous(ItemParams, ItemBuiltData);

			if (ItemBuiltData.ContainsOnlyValidBuildOutputForItem(ItemPath))
			{
				// Defer integration until the item build output is transformed into the
				// collection-level groom build output in Phase 3d+4d.
				FMetaHumanCrowdDeferredItemBuild& Deferred = Locals.DeferredGroomBuilds.Add(ItemPath);
				Deferred.SlotName = Item.SlotName;
				Deferred.BuiltData = MoveTemp(ItemBuiltData);
			}
		}
	}

	// -------------------------------------------------------------------------
	// Phase 1: Extract geometry bundles from character build outputs
	// -------------------------------------------------------------------------

	// Face geometry bundles (keyed by head item key)
	TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdMeshGeometryBundle> FaceGeometryBundles;
	for (const TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdCharacterBuildOutput>& Pair : Locals.HeadCharacterBuildOutputs)
	{
		if (Pair.Value.FaceMesh)
		{
			FMetaHumanCrowdMeshGeometryBundle& Bundle = FaceGeometryBundles.Add(Pair.Key);
			UE::MetaHuman::CrowdEditorUtilities::ExtractGeometryBundle(Pair.Value.FaceMesh, Bundle);
		}
	}

	// Body geometry bundles (keyed by body item key)
	TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdMeshGeometryBundle> BodyGeometryBundles;
	for (const TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdCharacterBuildOutput>& Pair : Locals.BodyCharacterBuildOutputs)
	{
		if (Pair.Value.BodyMesh)
		{
			FMetaHumanCrowdMeshGeometryBundle& Bundle = BodyGeometryBundles.Add(Pair.Key);
			UE::MetaHuman::CrowdEditorUtilities::ExtractGeometryBundle(Pair.Value.BodyMesh, Bundle);
		}
	}

	// -------------------------------------------------------------------------
	// Phase 2: CrowdBodyMerge on geometry bundles
	// -------------------------------------------------------------------------

	if (Params.Collection->GetQuality() == EMetaHumanCharacterPaletteBuildQuality::Production
		&& bApplyHiddenFaceMaps)
	{
		using namespace UE::MetaHuman;

		// Build body bundle pointer map for the merge
		TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdMeshGeometryBundle*> BodyBundlePtrMap;
		for (TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdMeshGeometryBundle>& Pair : BodyGeometryBundles)
		{
			BodyBundlePtrMap.Add(Pair.Key, &Pair.Value);
		}

		bool bAnySlotHasFaceMap = false;

		const UMetaHumanCharacterPipelineSpecification* PipelineSpec = GetRuntimePipeline()->GetSpecification();
		TArray<CrowdBodyMerge::FSlotData> SlotArray;
		{
			for (const TPair<FName, FMetaHumanCharacterPipelineSlot>& SpecSlotPair : PipelineSpec->Slots)
			{
				const TOptional<FName> RealSlotName = PipelineSpec->ResolveRealSlotName(SpecSlotPair.Key);
				if (!RealSlotName.IsSet())
				{
					continue;
				}

				const FMetaHumanCharacterPipelineSlot* RealSlot = PipelineSpec->Slots.Find(RealSlotName.GetValue());
				if (!RealSlot || RealSlot->BuildOutputStruct != FMetaHumanCrowdOutfitBuildOutput::StaticStruct())
				{
					continue;
				}

				CrowdBodyMerge::FSlotData& SlotData = SlotArray.AddDefaulted_GetRef();
				SlotData.VirtualSlotName = SpecSlotPair.Key;
				SlotData.SlotColor = SpecSlotPair.Value.SlotColor;

				if (SlotArray.Num() >= CrowdBodyMerge::UnclaimedSlotIndex)
				{
					UE_LOG(LogMetaHumanCrowdEditor, Warning, TEXT("CrowdBodyMerge: Too many clothing slots (max %d). Extra slots will be ignored."), CrowdBodyMerge::UnclaimedSlotIndex - 1);
					break;
				}
			}

			SlotArray.Sort([](const CrowdBodyMerge::FSlotData& A, const CrowdBodyMerge::FSlotData& B)
				{
					return A.VirtualSlotName.Compare(B.VirtualSlotName) < 0;
				});
		}

		// Collect outfit bundles grouped by virtual slot name.
		// Outfit item builds are held in Locals.DeferredOutfitBuilds, not integrated yet.
		for (TPair<FMetaHumanPaletteItemPath, FMetaHumanCrowdDeferredItemBuild>& DeferredPair : Locals.DeferredOutfitBuilds)
		{
			FMetaHumanPipelineBuiltData* ItemPipelineBuiltData = DeferredPair.Value.BuiltData.ItemBuiltData.MutableView().Find(DeferredPair.Key);
			if (!ItemPipelineBuiltData)
			{
				continue;
			}
			FMetaHumanCrowdOutfitBuildOutput* OutfitOutput = ItemPipelineBuiltData->BuildOutput.GetMutablePtr<FMetaHumanCrowdOutfitBuildOutput>();
			if (!OutfitOutput)
			{
				continue;
			}

			const FName VirtualSlotName = DeferredPair.Value.SlotName;
			CrowdBodyMerge::FSlotData* SlotData = SlotArray.FindByPredicate([VirtualSlotName](const CrowdBodyMerge::FSlotData& S) { return S.VirtualSlotName == VirtualSlotName; });
			if (!SlotData)
			{
				continue;
			}

			CrowdBodyMerge::FSlotItemData& ItemData = SlotData->Items.AddDefaulted_GetRef();

			for (TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdMeshGeometryBundle>& OutfitPair : OutfitOutput->BodyToOutfitGeometryMap)
			{
				ItemData.BodyToOutfitGeometryMap.Add(OutfitPair.Key, &OutfitPair.Value);
			}

			// Convert hidden face map texture to CPU image
			if (OutfitOutput->BodyHiddenFaceMap.Texture)
			{
				TArray<UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture> FaceMapTextures;
				FaceMapTextures.Add(OutfitOutput->BodyHiddenFaceMap);

				TArray<UE::MetaHuman::GeometryRemoval::FHiddenFaceMapImage> FaceMapImages;
				FText FailureReason;

				if (UE::MetaHuman::GeometryRemoval::TryConvertHiddenFaceMapTexturesToImages(FaceMapTextures, FaceMapImages, FailureReason)
					&& FaceMapImages.Num() > 0)
				{
					ItemData.BodyHiddenFaceMapImage = MoveTemp(FaceMapImages[0]);
					ItemData.bHasValidFaceMap = true;
					bAnySlotHasFaceMap = true;
				}
			}
		}

		if (bAnySlotHasFaceMap && BodyBundlePtrMap.Num() > 0)
		{
			// LODs to process = union of actor and instanced body LODs
			TSet<int32> LODSet;
			for (const FMetaHumanCrowdActorLODSettings& Entry : ActorBodyLODs)
			{
				LODSet.Add(Entry.SourceLOD);
			}
			for (const FMetaHumanCrowdInstancedLODSettings& Entry : InstancedBodyLODs)
			{
				LODSet.Add(Entry.SourceLOD);
			}
			TArray<int32> LODIndicesToProcess = LODSet.Array();
			LODIndicesToProcess.Sort();

			const FString DebugImagePath = FPaths::ProjectSavedDir() / FString::Printf(TEXT("SlotOwnership_%s.png"), *Params.Collection->GetName());

			CrowdBodyMerge::MergeBodyOntoOutfits(BodyBundlePtrMap, SlotArray, LODIndicesToProcess, TargetSkeleton, DebugImagePath);
			GetCollectionBuildOutput().bBodyGeometryMergedOntoClothing = true;
		}
	}

	// -------------------------------------------------------------------------
	// Phase 3: Construct INSTANCED meshes from geometry bundles
	// -------------------------------------------------------------------------

	using namespace UE::MetaHuman;

	// Collection-level head/body outputs, accumulated across Phases 3 - 5 and integrated at the end.
	TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdCollectionHeadBuildOutput> HeadCollectionBuildOutputs;
	TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdCollectionBodyBuildOutput> BodyCollectionBuildOutputs;

	// Record the face material slots the runtime should wrap in MIDs, plus the per-slot
	// custom-data offset layout (used to write per-instance face color params into the face
	// ISKM's custom-data buffer at AssembleCollection time).
	{
		FMetaHumanCrowdCollectionBuildOutput& CollectionBuildOutput = GetCollectionBuildOutput();
		TArray<FName>& SlotNames = CollectionBuildOutput.FaceMaterialSlotsForRuntimeMID;
		TMap<FName, FMetaHumanCrowdOutfitInstancedMaterial>& SlotLayout = CollectionBuildOutput.FaceSlotCustomDataLayout;
		SlotNames.Reset();
		SlotLayout.Reset();

		for (const FMetaHumanCrowdFaceMaterialOverride& Override : FaceMaterialOverrides)
		{
			if (Override.SlotName.IsNone() || (Override.ActorMaterial.IsNull() && Override.InstancedMaterial.IsNull()))
			{
				continue;
			}

			SlotNames.AddUnique(Override.SlotName);

			if (!Override.InstanceParameterNameToCustomDataFormat.IsEmpty())
			{
				FMetaHumanCrowdOutfitInstancedMaterial& Entry = SlotLayout.FindOrAdd(Override.SlotName);
				Entry.InstancedComponentMaterial = nullptr;
				Entry.InstanceParameterNameToCustomDataFormat = Override.InstanceParameterNameToCustomDataFormat;
			}
		}
	}

	// Emit per-(groom, head) face-side parameter bindings, one per groom item that targets a
	// head, so the runtime AssembleCollection can write hair-colour and AttributeMap parameter
	// values onto the face MIDs. Each binding's parameters are a parallel of the groom's
	// RuntimeMaterialParameters with MaterialParameter.Name rewritten to {WardrobeSlot}{OriginalName},
	// SlotNames pointing at the wrapped face slots, and an additional {WardrobeSlot}AttributeMap
	// entry that takes the groom's BakedGroomTexture as the default value.
	{
		TArray<FMetaHumanCrowdGroomFaceParameterBinding>& Bindings = GetCollectionBuildOutput().GroomFaceParameterBindings;
		Bindings.Reset();

		const TArray<FName>& WrappedSlots = GetCollectionBuildOutput().FaceMaterialSlotsForRuntimeMID;

		if (!WrappedSlots.IsEmpty())
		{
			for (TPair<FMetaHumanPaletteItemPath, FMetaHumanCrowdDeferredItemBuild>& DeferredPair : Locals.DeferredGroomBuilds)
			{
				const FMetaHumanPaletteItemPath& GroomItemPath = DeferredPair.Key;
				const FMetaHumanCrowdDeferredItemBuild& Deferred = DeferredPair.Value;
				const FMetaHumanPipelineBuiltData* GroomBuiltData = Deferred.BuiltData.ItemBuiltData.View().Find(GroomItemPath);

				if (!GroomBuiltData)
				{
					continue;
				}

				const FMetaHumanCrowdGroomBuildOutput* GroomOutput = GroomBuiltData->BuildOutput.GetPtr<FMetaHumanCrowdGroomBuildOutput>();

				if (!GroomOutput)
				{
					continue;
				}

				// Resolve the groom's runtime pipeline so we can read its RuntimeMaterialParameters.
				// Grooms are top-level items, so the path's first entry is the leaf key.
				if (GroomItemPath.GetNumPathEntries() == 0)
				{
					continue;
				}

				const FMetaHumanPaletteItemKey GroomLeafKey = GroomItemPath.GetPathEntry(0);
				const FMetaHumanCharacterPaletteItem* CollectionItem = Params.Collection->GetItems().FindByPredicate(
					[&GroomLeafKey](const FMetaHumanCharacterPaletteItem& Candidate)
					{
						return Candidate.GetItemKey() == GroomLeafKey;
					});
				
				if (!CollectionItem || !CollectionItem->WardrobeItem)
				{
					continue;
				}
				
				const UMetaHumanCrowdGroomPipeline* GroomRuntimePipeline =
					Cast<UMetaHumanCrowdGroomPipeline>(CollectionItem->WardrobeItem->GetPipeline());
				
				if (!GroomRuntimePipeline)
				{
					continue;
				}

				const FName PipelineSlotName = CollectionItem->SlotName;
				const FString PipelineSlotPrefix = PipelineSlotName.ToString();
				TArray<FMetaHumanMaterialParameter> PrefixedParameters;
				PrefixedParameters.Reserve(GroomRuntimePipeline->RuntimeMaterialParameters.Num());

				for (const FMetaHumanMaterialParameter& SrcParam : GroomRuntimePipeline->RuntimeMaterialParameters)
				{
					FMetaHumanMaterialParameter& DstParam = PrefixedParameters.Add_GetRef(SrcParam);

					// Face material parameter names follow the "{WardrobeSlot}{ParamName}" convention
					// (e.g. "HairMelanin", "BeardDyeColor"). Source params on
					// UMetaHumanCrowdGroomPipelineMaterialParameters use the unprefixed names.
					DstParam.MaterialParameter.Name = FName(*FString::Format(TEXT("{0}{1}"),
						{ PipelineSlotPrefix, SrcParam.MaterialParameter.Name.ToString() }));

					DstParam.SlotTarget = EMetaHumanRuntimeMaterialParameterSlotTarget::SlotNames;
					DstParam.SlotNames = WrappedSlots;
				}

				if (PrefixedParameters.IsEmpty())
				{
					continue;
				}

				// One binding per (groom, head). Heads are the keys of HeadGroomFitTargets that
				// the groom build was given.
				for (const TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdGroomFitTarget>& HeadPair : Locals.HeadGroomFitTargets)
				{
					FMetaHumanCrowdGroomFaceParameterBinding& Binding = Bindings.AddDefaulted_GetRef();
					Binding.GroomItemPath = GroomItemPath;
					Binding.HeadItemKey = HeadPair.Key;
					Binding.PipelineSlotName = PipelineSlotName;
					Binding.Parameters = PrefixedParameters;
				}

				// Strands-only UI fix: populate the property bag from the face material so the
				// MetaHuman Instance UI shows sliders for grooms with no card/helmet geometry.
				FMetaHumanPipelineBuiltData* MutableGroomBuiltData = DeferredPair.Value.BuiltData.ItemBuiltData.MutableView().Find(GroomItemPath);

				if (MutableGroomBuiltData && !MutableGroomBuiltData->AssemblyParameters.IsValid())
				{
					// Use any wrapped slot's override material to query parameters - assumes all
					// wrapped overrides share a parent so the bag reflects a coherent set.
					// Prefer the instanced parent (matches the ISKM path); fall back to actor
					// if the override only authored the actor variant.
					const FMetaHumanCrowdFaceMaterialOverride* AnyOverride = FaceMaterialOverrides.FindByPredicate(
						[&WrappedSlots](const FMetaHumanCrowdFaceMaterialOverride& Candidate)
						{
							return WrappedSlots.Contains(Candidate.SlotName)
								&& (!Candidate.InstancedMaterial.IsNull() || !Candidate.ActorMaterial.IsNull());
						});
					UMaterialInterface* SyntheticFaceMaterial = nullptr;
					if (AnyOverride)
					{
						SyntheticFaceMaterial = !AnyOverride->InstancedMaterial.IsNull()
							? AnyOverride->InstancedMaterial.LoadSynchronous()
							: AnyOverride->ActorMaterial.LoadSynchronous();
					}
					if (SyntheticFaceMaterial)
					{
						const auto FetchSyntheticName = UE::MetaHuman::MaterialUtils::FFetchSlotNameDelegate::CreateLambda(
							[SlotName = PipelineSlotName](int32 InSlotIndex)
							{
								return SlotName;
							});
						const auto FetchSyntheticMaterial = UE::MetaHuman::MaterialUtils::FFetchSlotMaterialDelegate::CreateLambda(
							[SyntheticFaceMaterial](int32 InSlotIndex) -> const UMaterialInterface*
							{
								return SyntheticFaceMaterial;
							});

						UE::MetaHuman::MaterialUtils::GenerateAssemblyParameters(
							{},
							PrefixedParameters,
							/*NumMaterialSlots=*/ 1,
							FetchSyntheticName,
							FetchSyntheticMaterial,
							MutableGroomBuiltData->AssemblyParameters);
					}
				}
			}
		}
	}

	// 3a. Instanced face meshes
	for (TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdMeshGeometryBundle>& FacePair : FaceGeometryBundles)
	{
		CrowdEditorUtilities::FMeshConstructionParams InstancedFaceParams;
		InstancedFaceParams.LODsToKeep = InstancedFaceSourceLODs;
		InstancedFaceParams.ScreenSizes = InstancedFaceScreenSizes;
		InstancedFaceParams.TargetSkeleton = TargetSkeleton;
		InstancedFaceParams.bEnableNanite = bEnableNaniteOnInstancedMeshes;
		InstancedFaceParams.bOptimizeForInstancing = true;
		InstancedFaceParams.BoneInfluenceLimit = 4;
		InstancedFaceParams.bRemoveTranslucentSections = true;
		InstancedFaceParams.SectionsToDisableShadow = {
			FMetaHumanCharacterSkinMaterials::EyeLeftSlotName,
			FMetaHumanCharacterSkinMaterials::EyeRightSlotName,
			FMetaHumanCharacterSkinMaterials::TeethSlotName };
		if (!bKeepTeethInInstancedMeshes)
		{
			InstancedFaceParams.SectionsToRemove = { FMetaHumanCharacterSkinMaterials::TeethSlotName };
		}

		// Align the instanced face mesh's body chain to the compatible body's ref pose.
		if (const FMetaHumanPaletteItemKey* CompatibleBodyKey = CompatibleBodyMap.Find(FacePair.Key))
		{
			if (const FMetaHumanCrowdMeshGeometryBundle* BodyBundle = BodyGeometryBundles.Find(*CompatibleBodyKey))
			{
				InstancedFaceParams.AlignmentRefSkeleton = &BodyBundle->RefSkeleton;
				InstancedFaceParams.AlignmentBoneName = HeadAlignmentBoneName;
			}
		}

		FMetaHumanCrowdCollectionHeadBuildOutput& HeadBuildOutput = HeadCollectionBuildOutputs.FindOrAdd(FacePair.Key);
		HeadBuildOutput.InstancedFaceMesh = CrowdEditorUtilities::ConstructMeshFromBundle(
			FacePair.Value, InstancedFaceParams, Params.OuterForGeneratedAssets);

		if (HeadBuildOutput.InstancedFaceMesh)
		{
			UE::MetaHuman::CrowdMaterialEditorUtils::ApplySlotMaterialOverrides(
				HeadBuildOutput.InstancedFaceMesh,
				FaceMaterialOverrides,
				TEXT("Face"),
				FacePair.Key.ToAssetNameString(),
				UE::MetaHuman::CrowdMaterialEditorUtils::EFaceMeshVariant::Instanced,
				Params.OuterForGeneratedAssets);

			UE::MetaHuman::CrowdMaterialEditorUtils::BuildSlotSourceLODsMap(
				HeadBuildOutput.InstancedFaceMesh,
				InstancedFaceSourceLODs,
				HeadBuildOutput.InstancedFaceSlotSourceLODs);

			UE::MetaHuman::CrowdEditorUtilities::ApplyMinLODToMesh(
				HeadBuildOutput.InstancedFaceMesh, InstancedMeshMinLOD, InstancedMeshQualityLevelMinLOD);

			// Snapshot per-slot MICs after ApplySlotMaterialOverrides has reparented them.
			// BuildFaceMICCombos uses these as parents for the generated combo MICs.
			{
				TMap<FName, TObjectPtr<UMaterialInterface>>& SourceMICs = HeadBuildOutput.InstancedFaceSourceMICs;
				SourceMICs.Reset();
				for (const FName& SlotName : GetCollectionBuildOutput().FaceMaterialSlotsForRuntimeMID)
				{
					for (const FSkeletalMaterial& Mat : HeadBuildOutput.InstancedFaceMesh->GetMaterials())
					{
						if (Mat.MaterialSlotName == SlotName)
						{
							SourceMICs.Add(SlotName, Mat.MaterialInterface);
							break;
						}
					}
				}
			}
		}
	}

	// 3b. Instanced body meshes
	for (TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdMeshGeometryBundle>& BodyPair : BodyGeometryBundles)
	{
		FMetaHumanCrowdCollectionBodyBuildOutput& BodyBuildOutput = BodyCollectionBuildOutputs.FindOrAdd(BodyPair.Key);

		// Skip instanced body mesh if body geometry was merged onto clothing
		if (!GetCollectionBuildOutput().bBodyGeometryMergedOntoClothing)
		{
			CrowdEditorUtilities::FMeshConstructionParams InstancedBodyParams;
			InstancedBodyParams.LODsToKeep = InstancedBodySourceLODs;
			InstancedBodyParams.ScreenSizes = InstancedBodyScreenSizes;
			InstancedBodyParams.TargetSkeleton = TargetSkeleton;
			InstancedBodyParams.bEnableNanite = bEnableNaniteOnInstancedMeshes;
			InstancedBodyParams.bOptimizeForInstancing = true;
			InstancedBodyParams.BoneInfluenceLimit = 4;
			InstancedBodyParams.bRemoveTranslucentSections = true;
			InstancedBodyParams.ForceKeepBoneNames = BodyForceKeepBones;

			BodyBuildOutput.InstancedBodyMesh = CrowdEditorUtilities::ConstructMeshFromBundle(
				BodyPair.Value, InstancedBodyParams, Params.OuterForGeneratedAssets);

			UE::MetaHuman::CrowdEditorUtilities::ApplyMinLODToMesh(
				BodyBuildOutput.InstancedBodyMesh, InstancedMeshMinLOD, InstancedMeshQualityLevelMinLOD);
		}
	}

	// 3c+4c. Outfit meshes (both instanced and actor, built from deferred item builds, then
	// the item-pipeline build output is transformed into the collection-level output and integrated).
	for (TPair<FMetaHumanPaletteItemPath, FMetaHumanCrowdDeferredItemBuild>& DeferredPair : Locals.DeferredOutfitBuilds)
	{
		const FMetaHumanPaletteItemPath& OutfitItemPath = DeferredPair.Key;
		FMetaHumanCrowdDeferredItemBuild& Deferred = DeferredPair.Value;

		FMetaHumanPipelineBuiltData* ItemPipelineBuiltData = Deferred.BuiltData.ItemBuiltData.MutableView().Find(OutfitItemPath);
		if (!ItemPipelineBuiltData)
		{
			continue;
		}
		const FMetaHumanCrowdOutfitBuildOutput* OutfitOutput = ItemPipelineBuiltData->BuildOutput.GetPtr<FMetaHumanCrowdOutfitBuildOutput>();
		if (!OutfitOutput)
		{
			continue;
		}

		TMap<FMetaHumanPaletteItemKey, TObjectPtr<USkeletalMesh>> ActorMeshMap;
		TMap<FMetaHumanPaletteItemKey, TObjectPtr<USkeletalMesh>> InstancedMeshMap;

		for (const TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdMeshGeometryBundle>& BundlePair : OutfitOutput->BodyToOutfitGeometryMap)
		{
			// Instanced outfit mesh
			{
				CrowdEditorUtilities::FMeshConstructionParams InstancedParams;
				InstancedParams.LODsToKeep = InstancedBodySourceLODs;
				InstancedParams.ScreenSizes = InstancedBodyScreenSizes;
				InstancedParams.TargetSkeleton = TargetSkeleton;
				InstancedParams.bEnableNanite = bEnableNaniteOnInstancedMeshes;
				InstancedParams.bOptimizeForInstancing = true;
				InstancedParams.BoneInfluenceLimit = 4;
				InstancedParams.bRemoveTranslucentSections = true;
				InstancedParams.ForceKeepBoneNames = BodyForceKeepBones;

				// Scale the body LOD screen size thresholds for the outfit's bounds, so it'll
				// switch LODs at roughly the same camera distance as the body it pairs with.
				//
				// The BoxExtent is the same measure that GPU LOD selection uses, so this should
				// be pretty accurate.
				if (const FMetaHumanCrowdMeshGeometryBundle* PairedBodyBundle = BodyGeometryBundles.Find(BundlePair.Key))
				{
					if (PairedBodyBundle->MeshDescriptions.IsValidIndex(InstancedBodySourceLODs[0])
						&& BundlePair.Value.MeshDescriptions.IsValidIndex(InstancedBodySourceLODs[0]))
					{
						const float BodyExtent = PairedBodyBundle->MeshDescriptions[InstancedBodySourceLODs[0]].GetBounds().BoxExtent.Length();
						const float OutfitExtent = BundlePair.Value.MeshDescriptions[InstancedBodySourceLODs[0]].GetBounds().BoxExtent.Length();
						if (BodyExtent > UE_SMALL_NUMBER)
						{
							InstancedParams.ScreenSizeScaleFactor = OutfitExtent / BodyExtent;
						}
					}
				}

				USkeletalMesh* Mesh = CrowdEditorUtilities::ConstructMeshFromBundle(
					BundlePair.Value, InstancedParams, Params.OuterForGeneratedAssets);
				if (Mesh)
				{
					UE::MetaHuman::CrowdEditorUtilities::ApplyMinLODToMesh(
						Mesh, InstancedMeshMinLOD, InstancedMeshQualityLevelMinLOD);

					InstancedMeshMap.Add(BundlePair.Key, Mesh);
				}
			}

			// Actor outfit mesh
			{
				CrowdEditorUtilities::FMeshConstructionParams ActorParams;
				ActorParams.LODsToKeep = ActorBodySourceLODs;
				ActorParams.TargetSkeleton = TargetSkeleton;
				ActorParams.ForceKeepBoneNames = BodyForceKeepBones;

				USkeletalMesh* Mesh = CrowdEditorUtilities::ConstructMeshFromBundle(
					BundlePair.Value, ActorParams, Params.OuterForGeneratedAssets);
				if (Mesh)
				{
					UE::MetaHuman::CrowdEditorUtilities::ApplyMinLODToMesh(
						Mesh, ActorMeshMinLOD, ActorMeshQualityLevelMinLOD);

					ActorMeshMap.Add(BundlePair.Key, Mesh);
				}
			}
		}

		// Build a new palette built data containing the collection-level outfit output and
		// integrate it into the main built data. Preserve DefaultUnpackSubfolder and Metadata
		// from the original item build so unpack paths and asset tracking survive.
		FMetaHumanPaletteBuiltData CollectionLevelBuiltData;
		FMetaHumanPipelineBuiltData& CollectionLevelPipelineBuiltData = CollectionLevelBuiltData.ItemBuiltData.Edit().Add(OutfitItemPath);
		FMetaHumanCrowdCollectionOutfitBuildOutput& CollectionOutfitOutput = CollectionLevelPipelineBuiltData.BuildOutput.InitializeAs<FMetaHumanCrowdCollectionOutfitBuildOutput>();
		CollectionOutfitOutput.BodyToActorOutfitMeshMap = MoveTemp(ActorMeshMap);
		CollectionOutfitOutput.BodyToInstancedOutfitMeshMap = MoveTemp(InstancedMeshMap);
		CollectionLevelPipelineBuiltData.DefaultUnpackSubfolder = MoveTemp(ItemPipelineBuiltData->DefaultUnpackSubfolder);
		CollectionLevelPipelineBuiltData.bDefaultSubfolderIsAbsolute = ItemPipelineBuiltData->bDefaultSubfolderIsAbsolute;
		CollectionLevelPipelineBuiltData.Metadata = MoveTemp(ItemPipelineBuiltData->Metadata);
		CollectionLevelPipelineBuiltData.AssemblyParameters = MoveTemp(ItemPipelineBuiltData->AssemblyParameters);

		BuiltData->PaletteBuiltData.IntegrateItemBuiltData(OutfitItemPath, Deferred.SlotName, MoveTemp(CollectionLevelBuiltData));
	}
	Locals.DeferredOutfitBuilds.Empty();

	// 3d+4d. Groom meshes (both instanced and actor, built from deferred item builds, then
	// the item-pipeline build output is transformed into the collection-level output and integrated).
	for (TPair<FMetaHumanPaletteItemPath, FMetaHumanCrowdDeferredItemBuild>& DeferredPair : Locals.DeferredGroomBuilds)
	{
		const FMetaHumanPaletteItemPath& GroomItemPath = DeferredPair.Key;
		FMetaHumanCrowdDeferredItemBuild& Deferred = DeferredPair.Value;

		FMetaHumanPipelineBuiltData* ItemPipelineBuiltData = Deferred.BuiltData.ItemBuiltData.MutableView().Find(GroomItemPath);
		if (!ItemPipelineBuiltData)
		{
			continue;
		}
		const FMetaHumanCrowdGroomBuildOutput* GroomOutput = ItemPipelineBuiltData->BuildOutput.GetPtr<FMetaHumanCrowdGroomBuildOutput>();
		if (!GroomOutput)
		{
			continue;
		}

		TMap<FMetaHumanPaletteItemKey, TObjectPtr<USkeletalMesh>> ActorMeshMap;
		TMap<FMetaHumanPaletteItemKey, TObjectPtr<USkeletalMesh>> InstancedMeshMap;

		TArray<FMetaHumanGeneratedAssetMetadata> GroomGeneratedAssetMetadata;
		const FString GroomAssetName = GroomOutput->GroomAssetName;

		UMaterialInterface* LoadedInvisibleMaterial = InvisibleMaterial.LoadSynchronous();

		// One geometry bundle per (groom, head) pair, with a parallel per-key override map for
		// the instanced variant's MICs. Geometry is shared; the actor mesh consumes the bundle
		// directly, the instanced mesh consumes a copy of the bundle with Materials/LODMaterialMaps
		// swapped to the instanced override. Two MIC sets per key because actor and ISKM
		// consume different LOD subsets and any mix of card-vs-helmet source-LOD is possible
		// (card-card, helmet-card, card-helmet, helmet-helmet); cards and helmets need distinct
		// MICs (different parent material), and same-kind MICs across variants stay as
		// independent UObjects so neither skeletal mesh references the other's materials.
		for (const TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdMeshGeometryBundle>& BundlePair : GroomOutput->ItemToGroomGeometryMap)
		{
			const FMetaHumanPaletteItemKey& ItemKey = BundlePair.Key;
			const FString HeadAssetName = ItemKey.ToAssetNameString();
			const FMetaHumanCrowdMeshGeometryBundle& ActorBundle = BundlePair.Value;
			const FMetaHumanCrowdGroomMaterialOverride* InstancedOverride = GroomOutput->InstancedMaterialOverrides.Find(ItemKey);

			// Cards/helmets at face LODs >= GroomTextureMinLOD render the baked-groom texture on the
			// face material instead of competing geometry. Geometry is left in place at those LODs
			// (so LOD switching keeps working) but its material slots are swapped to a mask
			// material that draws nothing. INT32_MAX (default when no baked entry exists) means no
			// masking happens.
			const FMetaHumanCrowdGroomBakedTextureEntry* BakedEntry = GroomOutput->ItemToBakedGroomTexture.Find(ItemKey);
			const int32 GroomGroomTextureMinLOD = BakedEntry
				? BakedEntry->GroomTextureMinLOD
				: INT32_MAX;

			// Instanced groom mesh
			{
				CrowdEditorUtilities::FMeshConstructionParams InstancedParams;
				InstancedParams.TargetSkeleton = TargetSkeleton;
				InstancedParams.bEnableNanite = bEnableNaniteOnInstancedMeshes;
				InstancedParams.bOptimizeForInstancing = true;
				InstancedParams.BoneInfluenceLimit = 4;
				InstancedParams.bRemoveTranslucentSections = true;
				InstancedParams.ForceKeepBoneNames = BodyForceKeepBones;

				// Request any LODs that the groom has available
				InstancedParams.LODsToKeep.Reserve(InstancedFaceLODs.Num());
				for (const FMetaHumanCrowdInstancedLODSettings& FaceLODSettings : InstancedFaceLODs)
				{
					const int32 FaceLOD = FaceLODSettings.SourceLOD;
					if (ActorBundle.MeshDescriptions.IsValidIndex(FaceLOD))
					{
						InstancedParams.LODsToKeep.Add(FaceLOD);
					}
					else
					{
						// Bail out at the first missing LOD, so that the groom LOD indices are
						// aligned with the face LOD indices.
						break;
					}
				}

				FMetaHumanCrowdMeshGeometryBundle InstancedBundle = ActorBundle;
				if (InstancedOverride)
				{
					InstancedBundle.Materials = InstancedOverride->Materials;
					InstancedBundle.LODMaterialMaps = InstancedOverride->LODMaterialMaps;
				}
				else
				{
					UE_LOGFMT(LogMetaHumanCrowdEditor, Warning,
						"ItemToGroomGeometryMap has entry for {Head}/{Groom} but InstancedMaterialOverrides does not. Falling back to actor materials for the instanced groom mesh.",
						HeadAssetName, GroomAssetName);
				}

				// Align the instanced groom mesh's body chain to the head's compatible body's ref pose
				if (const FMetaHumanPaletteItemKey* CompatibleBodyKey = CompatibleBodyMap.Find(ItemKey))
				{
					if (const FMetaHumanCrowdMeshGeometryBundle* BodyBundle = BodyGeometryBundles.Find(*CompatibleBodyKey))
					{
						InstancedParams.AlignmentRefSkeleton = &BodyBundle->RefSkeleton;
						InstancedParams.AlignmentBoneName = HeadAlignmentBoneName;
					}
				}

				InstancedParams.ScreenSizes = InstancedFaceScreenSizes;

				// Scale the face LOD screen size thresholds for the groom's bounds, so it'll
				// switch LODs at roughly the same camera distance as the face it pairs with.
				//
				// The BoxExtent is the same measure that GPU LOD selection uses, so this should
				// be pretty accurate.
				if (!InstancedFaceSourceLODs.IsEmpty())
				{
					const int32 RefLOD = InstancedFaceSourceLODs[0];
					if (const FMetaHumanCrowdMeshGeometryBundle* PairedFaceBundle = FaceGeometryBundles.Find(ItemKey))
					{
						if (PairedFaceBundle->MeshDescriptions.IsValidIndex(RefLOD)
							&& InstancedBundle.MeshDescriptions.IsValidIndex(RefLOD))
						{
							const float FaceExtent = PairedFaceBundle->MeshDescriptions[RefLOD].GetBounds().BoxExtent.Length();
							const float GroomExtent = InstancedBundle.MeshDescriptions[RefLOD].GetBounds().BoxExtent.Length();
							if (FaceExtent > UE_SMALL_NUMBER)
							{
								InstancedParams.ScreenSizeScaleFactor = GroomExtent / FaceExtent;
							}
						}
					}
				}

				USkeletalMesh* Mesh = CrowdEditorUtilities::ConstructMeshFromBundle(
					InstancedBundle, InstancedParams, Params.OuterForGeneratedAssets);
				if (Mesh)
				{
					UE::MetaHuman::CrowdMaterialEditorUtils::ApplyMaskAtHighLODs(
						Mesh,
						InstancedParams.LODsToKeep,
						GroomGroomTextureMinLOD,
						LoadedInvisibleMaterial);

					UE::MetaHuman::CrowdEditorUtilities::ApplyMinLODToMesh(
						Mesh, InstancedMeshMinLOD, InstancedMeshQualityLevelMinLOD);

					InstancedMeshMap.Add(ItemKey, Mesh);
					GroomGeneratedAssetMetadata.Emplace(
						Mesh,
						ItemPipelineBuiltData->DefaultUnpackSubfolder,
						FString::Format(TEXT("SKM_{0}_{1}_Inst"), { GroomAssetName, HeadAssetName }));
				}
			}

			// Actor groom mesh
			{
				CrowdEditorUtilities::FMeshConstructionParams ActorParams;
				ActorParams.LODsToKeep = ActorFaceSourceLODs;
				ActorParams.TargetSkeleton = TargetSkeleton;
				ActorParams.ForceKeepBoneNames = BodyForceKeepBones;

				USkeletalMesh* Mesh = CrowdEditorUtilities::ConstructMeshFromBundle(
					ActorBundle, ActorParams, Params.OuterForGeneratedAssets);
				if (Mesh)
				{
					UE::MetaHuman::CrowdMaterialEditorUtils::ApplyMaskAtHighLODs(
						Mesh,
						ActorParams.LODsToKeep,
						GroomGroomTextureMinLOD,
						LoadedInvisibleMaterial);

					UE::MetaHuman::CrowdEditorUtilities::ApplyMinLODToMesh(
						Mesh, ActorMeshMinLOD, ActorMeshQualityLevelMinLOD);

					ActorMeshMap.Add(ItemKey, Mesh);
					GroomGeneratedAssetMetadata.Emplace(
						Mesh,
						ItemPipelineBuiltData->DefaultUnpackSubfolder,
						FString::Format(TEXT("SKM_{0}_{1}_Actor"), { GroomAssetName, HeadAssetName }));
				}
			}
		}

		// Build a new palette built data containing the collection-level groom output and
		// integrate it into the main built data.
		FMetaHumanPaletteBuiltData CollectionLevelBuiltData;
		FMetaHumanPipelineBuiltData& CollectionLevelPipelineBuiltData = CollectionLevelBuiltData.ItemBuiltData.Edit().Add(GroomItemPath);
		FMetaHumanCrowdCollectionGroomBuildOutput& CollectionGroomOutput = CollectionLevelPipelineBuiltData.BuildOutput.InitializeAs<FMetaHumanCrowdCollectionGroomBuildOutput>();
		CollectionGroomOutput.ItemToActorGroomMeshMap = MoveTemp(ActorMeshMap);
		CollectionGroomOutput.ItemToInstancedGroomMeshMap = MoveTemp(InstancedMeshMap);

		if (FMetaHumanCrowdGroomBuildOutput* GroomOutputPtr = ItemPipelineBuiltData->BuildOutput.GetMutablePtr<FMetaHumanCrowdGroomBuildOutput>())
		{
			CollectionGroomOutput.ItemToBakedGroomTexture = MoveTemp(GroomOutputPtr->ItemToBakedGroomTexture);
			CollectionGroomOutput.PipelineSlotName = GroomOutputPtr->PipelineSlotName;
		}
		CollectionLevelPipelineBuiltData.DefaultUnpackSubfolder = MoveTemp(ItemPipelineBuiltData->DefaultUnpackSubfolder);
		CollectionLevelPipelineBuiltData.bDefaultSubfolderIsAbsolute = ItemPipelineBuiltData->bDefaultSubfolderIsAbsolute;
		CollectionLevelPipelineBuiltData.Metadata = MoveTemp(ItemPipelineBuiltData->Metadata);
		CollectionLevelPipelineBuiltData.Metadata.Append(MoveTemp(GroomGeneratedAssetMetadata));
		CollectionLevelPipelineBuiltData.AssemblyParameters = MoveTemp(ItemPipelineBuiltData->AssemblyParameters);

		BuiltData->PaletteBuiltData.IntegrateItemBuiltData(GroomItemPath, Deferred.SlotName, MoveTemp(CollectionLevelBuiltData));
	}
	Locals.DeferredGroomBuilds.Empty();

	// -------------------------------------------------------------------------
	// Phase 4: Construct ACTOR meshes from geometry bundles
	// -------------------------------------------------------------------------

	// 4a. Actor face meshes

	// Build a list of material slot names used for the face skin on different LODs 
	TArray<FName, TInlineAllocator<static_cast<int32>(EMetaHumanCharacterSkinMaterialSlot::Count)>> SkinSlotNames;
	for (EMetaHumanCharacterSkinMaterialSlot Slot : TEnumRange<EMetaHumanCharacterSkinMaterialSlot>())
	{
		SkinSlotNames.Add(FMetaHumanCharacterSkinMaterials::GetSkinMaterialSlotName(Slot));
	}

	for (TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdMeshGeometryBundle>& FacePair : FaceGeometryBundles)
	{
		const FMetaHumanPaletteItemKey& ItemKey = FacePair.Key;

		USkeletalMesh* SourceDNAMesh = Locals.HeadCharacterBuildOutputs.Contains(ItemKey)
			? Locals.HeadCharacterBuildOutputs[ItemKey].FaceMesh
			: nullptr;

		CrowdEditorUtilities::FMeshConstructionParams ActorFaceParams;
		ActorFaceParams.LODsToKeep = ActorFaceSourceLODs;
		ActorFaceParams.TargetSkeleton = TargetSkeleton;
		ActorFaceParams.bPreserveDNA = (SourceDNAMesh != nullptr);
		ActorFaceParams.SourceDNAMesh = SourceDNAMesh;
		ActorFaceParams.RecomputeTangentsLODIndexThreshold = FaceRecomputeTangentsLODThreshold;
		ActorFaceParams.RecomputeTangentsMaterialSlotNames = TArray<FName>(SkinSlotNames);

		FMetaHumanCrowdCollectionHeadBuildOutput& HeadBuildOutput = HeadCollectionBuildOutputs.FindOrAdd(ItemKey);
		HeadBuildOutput.ActorFaceMesh = CrowdEditorUtilities::ConstructMeshFromBundle(
			FacePair.Value, ActorFaceParams, Params.OuterForGeneratedAssets);

		if (!HeadBuildOutput.ActorFaceMesh)
		{
			continue;
		}

		UE::MetaHuman::CrowdMaterialEditorUtils::ApplySlotMaterialOverrides(
			HeadBuildOutput.ActorFaceMesh,
			FaceMaterialOverrides,
			TEXT("Face"),
			ItemKey.ToAssetNameString(),
			UE::MetaHuman::CrowdMaterialEditorUtils::EFaceMeshVariant::Actor,
			Params.OuterForGeneratedAssets);

		UE::MetaHuman::CrowdMaterialEditorUtils::BuildSlotSourceLODsMap(
			HeadBuildOutput.ActorFaceMesh,
			ActorFaceSourceLODs,
			HeadBuildOutput.ActorFaceSlotSourceLODs);

		UE::MetaHuman::CrowdEditorUtilities::ApplyMinLODToMesh(
			HeadBuildOutput.ActorFaceMesh, ActorMeshMinLOD, ActorMeshQualityLevelMinLOD);

		if (SourceDNAMesh)
		{
			if (FacialAnimationLODThreshold >= 0)
			{
				HeadBuildOutput.ActorFaceMesh->SetPostProcessAnimBlueprint(SourceDNAMesh->GetPostProcessAnimBlueprint());
				HeadBuildOutput.ActorFaceMesh->SetPostProcessAnimGraphLODThreshold(FacialAnimationLODThreshold);
			}

			if (UPhysicsAsset* SourcePhysicsAsset = SourceDNAMesh->GetPhysicsAsset())
			{
				HeadBuildOutput.ActorFaceMesh->SetPhysicsAsset(SourcePhysicsAsset);
			}
		}
	}

	// 4a-bis. Bake face MICs per (head, equipped-groom) combo.
	//
	// Replaces the per-Assemble UMaterialInstanceDynamic creation in the runtime AssembleCollection
	// path with shared UMaterialInstanceConstant pointers, so UMetaHumanInstances that resolve to
	// the same combo render with the same material asset.
	//
	// Combo key dimensions: (HeadKey, [(PipelineSlot, EquippedGroomKey)...]) where the slot list
	// is the set of pipeline slots whose items contribute a {Slot}AttributeMap binding to the
	// face material. A slot can also carry a default-keyed sentinel meaning "nothing equipped".
	{
		FMetaHumanCrowdCollectionBuildOutput& CollectionBuildOutput = GetCollectionBuildOutput();
		CollectionBuildOutput.FaceAffectingPipelineSlots.Reset();
		CollectionBuildOutput.FaceMICsByCombo.Reset();

		const TArray<FName>& WrappedSlots = CollectionBuildOutput.FaceMaterialSlotsForRuntimeMID;

		// Per pipeline slot, the equipped-groom keys we'll enumerate against. The default-
		// constructed key is the "nothing equipped" sentinel.
		TMap<FName, TArray<FMetaHumanPaletteItemKey>> SlotToItemKeys;

		// Per (head, equipped-groom key), the corresponding FMetaHumanCrowdGroomBakedTextureEntry
		// (Texture + PipelineSlotName + AppliesAtFaceLODs + MinLOD). Resolved up front so the
		// combo-baking loop just looks it up.
		using UE::MetaHuman::CrowdEditorPipelinePrivate::FGroomBakedKey;
		TMap<FGroomBakedKey, FMetaHumanCrowdGroomBakedTextureEntry> GroomBakedEntries;

		// Walk every groom item that contributes face bindings; record its leaf key under its
		// pipeline slot, and record its per-head baked entries.
		for (const FMetaHumanCrowdGroomFaceParameterBinding& Binding : CollectionBuildOutput.GroomFaceParameterBindings)
		{
			if (Binding.GroomItemPath.GetNumPathEntries() == 0)
			{
				continue;
			}

			const FMetaHumanPaletteItemKey GroomLeafKey = Binding.GroomItemPath.GetPathEntry(0);
			SlotToItemKeys.FindOrAdd(Binding.PipelineSlotName).AddUnique(GroomLeafKey);

			// Look up the (groom, head) baked-texture entry on the integrated collection groom output.
			if (const FMetaHumanPipelineBuiltData* GroomBuiltData = BuiltData->PaletteBuiltData.ItemBuiltData.View().Find(Binding.GroomItemPath))
			{
				if (const FMetaHumanCrowdCollectionGroomBuildOutput* GroomOutput = GroomBuiltData->BuildOutput.GetPtr<FMetaHumanCrowdCollectionGroomBuildOutput>())
				{
					if (const FMetaHumanCrowdGroomBakedTextureEntry* Entry = GroomOutput->ItemToBakedGroomTexture.Find(Binding.HeadItemKey))
					{
						GroomBakedEntries.Add(FGroomBakedKey{ Binding.HeadItemKey, GroomLeafKey }, *Entry);
					}
				}
			}
		}

		// Append the "nothing equipped" sentinel to every slot's enumeration list. The runtime
		// uses a default-constructed FMetaHumanPaletteItemKey for slots with no current selection.
		for (TPair<FName, TArray<FMetaHumanPaletteItemKey>>& Pair : SlotToItemKeys)
		{
			Pair.Value.AddUnique(FMetaHumanPaletteItemKey());
		}

		// Record the slot list (sorted) for the runtime to use when constructing combo keys.
		SlotToItemKeys.GetKeys(CollectionBuildOutput.FaceAffectingPipelineSlots);
		CollectionBuildOutput.FaceAffectingPipelineSlots.Sort(
			[](const FName& A, const FName& B)
			{
				return A.LexicalLess(B);
			});

		// Skip combo baking entirely when there's nothing to enumerate (e.g. no grooms target the
		// face). The runtime will then take the fallback path which still creates per-Assemble MIDs.
		if (CollectionBuildOutput.FaceAffectingPipelineSlots.IsEmpty() || WrappedSlots.IsEmpty())
		{
			// Nothing to bake.
		}
		else
		{
			using UE::MetaHuman::CrowdEditorPipelinePrivate::FMICContentKey;
			TMap<FMICContentKey, UMaterialInstanceConstant*> MICCache;

			// Resolve the {PipelineSlot -> AttributeMap texture} map for a given (face slot, combo)
			// pair. Returns the input texture for each slot in the combo (nullptr if the slot's
			// groom doesn't overlap the face slot's source LODs, or no groom is equipped).
			auto ResolveSlotTextures = [&](
				const FMetaHumanCrowdFaceSlotLODs* InSlotLODs,
				const FMetaHumanCrowdFaceMICComboKey& InComboKey) -> TArray<TTuple<FName, UTexture*>>
			{
				TArray<TTuple<FName, UTexture*>> Result;
				Result.Reserve(InComboKey.EquippedGrooms.Num());
				for (const FMetaHumanCrowdFaceMICComboSlot& Entry : InComboKey.EquippedGrooms)
				{
					UTexture* TextureToUse = nullptr;
					if (!Entry.EquippedGroomKey.IsNull())
					{
						if (const FMetaHumanCrowdGroomBakedTextureEntry* Entry2 = GroomBakedEntries.Find(FGroomBakedKey{ InComboKey.HeadKey, Entry.EquippedGroomKey }))
						{
							const bool bAnyOverlap = InSlotLODs && InSlotLODs->SourceLODs.ContainsByPredicate(
								[Entry2](int32 SourceLOD)
								{
									return Entry2->AppliesAtFaceLODs.Contains(SourceLOD);
								});
							if (bAnyOverlap)
							{
								TextureToUse = Entry2->Texture.Get();
							}
						}
					}
					Result.Emplace(Entry.PipelineSlotName, TextureToUse);
				}
				// Sort by slot name so the content key is order-independent.
				Result.Sort([](const TTuple<FName, UTexture*>& A, const TTuple<FName, UTexture*>& B)
				{
					return A.Key.LexicalLess(B.Key);
				});
				return Result;
			};

			// Bake (or reuse) one MIC for a (face slot, combo) pair. Cached on content key so
			// combos that produce byte-identical MICs share a single asset.
			int32 NextMICIndex = 0;
			auto BakeOrReuseMIC = [&](
				UMaterialInterface* InSourceMIC,
				const FName& InFaceSlotName,
				const FMetaHumanCrowdFaceSlotLODs* InSlotLODs,
				FStringView InVariantTag,
				const FMetaHumanCrowdFaceMICComboKey& InComboKey,
				const FString& InHeadAssetName) -> UMaterialInstanceConstant*
			{
				FMICContentKey ContentKey;
				ContentKey.Parent = InSourceMIC;
				ContentKey.SlotTextures = ResolveSlotTextures(InSlotLODs, InComboKey);

				if (UMaterialInstanceConstant* const* Existing = MICCache.Find(ContentKey))
				{
					return *Existing;
				}

				// Cache miss, bake a new MIC
				const FString DesiredName = FString::Format(
					TEXT("MI_FaceCombo_{0}_{1}_{2}_{3}"),
					{ InHeadAssetName, InFaceSlotName.ToString(), FString(InVariantTag), NextMICIndex++ });

				UMaterialInstanceConstant* NewMIC = NewObject<UMaterialInstanceConstant>(
					Params.OuterForGeneratedAssets, FName(*DesiredName), RF_Public);
				NewMIC->SetParentEditorOnly(InSourceMIC);

				for (const TTuple<FName, UTexture*>& Pair : ContentKey.SlotTextures)
				{
					const FName AttributeMapParam(*FString::Printf(TEXT("%sAttributeMap"), *Pair.Key.ToString()));
					NewMIC->SetTextureParameterValueEditorOnly(AttributeMapParam, Pair.Value);
				}

				MICCache.Add(MoveTemp(ContentKey), NewMIC);
				return NewMIC;
			};

			// Recursive cross-product enumeration over CollectionBuildOutput.FaceAffectingPipelineSlots.
			TArray<FMetaHumanCrowdFaceMICComboSlot> CurrentCombo;
			CurrentCombo.Reserve(CollectionBuildOutput.FaceAffectingPipelineSlots.Num());

			TFunction<void(int32, const FMetaHumanPaletteItemKey&, FMetaHumanCrowdCollectionHeadBuildOutput&)> Enumerate;
			Enumerate = [&](int32 SlotIdx, const FMetaHumanPaletteItemKey& HeadKey, FMetaHumanCrowdCollectionHeadBuildOutput& HeadBuildOutput)
			{
				if (SlotIdx == CollectionBuildOutput.FaceAffectingPipelineSlots.Num())
				{
					// Combo complete. Bake all wrapped slots for both variants.
					FMetaHumanCrowdFaceMICComboKey ComboKey;
					ComboKey.HeadKey = HeadKey;
					ComboKey.EquippedGrooms = CurrentCombo;

					FMetaHumanCrowdFaceMICSet& MICSet = CollectionBuildOutput.FaceMICsByCombo.Add(ComboKey);

					const FString HeadAssetName = HeadKey.ToAssetNameString();

					for (const FName& WrappedSlot : WrappedSlots)
					{
						if (TObjectPtr<UMaterialInterface>* InstSrc = HeadBuildOutput.InstancedFaceSourceMICs.Find(WrappedSlot))
						{
							if (*InstSrc)
							{
								const FMetaHumanCrowdFaceSlotLODs* SlotLODs = HeadBuildOutput.InstancedFaceSlotSourceLODs.Find(WrappedSlot);
								MICSet.InstancedSlotToMIC.Add(WrappedSlot,
									BakeOrReuseMIC(*InstSrc, WrappedSlot, SlotLODs, TEXTVIEW("Inst"), ComboKey, HeadAssetName));
							}
						}
					}
					return;
				}

				const FName& SlotName = CollectionBuildOutput.FaceAffectingPipelineSlots[SlotIdx];
				const TArray<FMetaHumanPaletteItemKey>* ItemKeys = SlotToItemKeys.Find(SlotName);
				if (!ItemKeys)
				{
					return;
				}

				for (const FMetaHumanPaletteItemKey& ItemKey : *ItemKeys)
				{
					FMetaHumanCrowdFaceMICComboSlot& Entry = CurrentCombo.AddDefaulted_GetRef();
					Entry.PipelineSlotName = SlotName;
					Entry.EquippedGroomKey = ItemKey;
					Enumerate(SlotIdx + 1, HeadKey, HeadBuildOutput);
					CurrentCombo.Pop(EAllowShrinking::No);
				}
			};

			// Enumerate per head.
			for (TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdCollectionHeadBuildOutput>& HeadPair : HeadCollectionBuildOutputs)
			{
				CurrentCombo.Reset();
				Enumerate(0, HeadPair.Key, HeadPair.Value);
			}
		}
	}

	// 4b. Actor body meshes
	for (TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdMeshGeometryBundle>& BodyPair : BodyGeometryBundles)
	{
		USkeletalMesh* SourceDNAMesh = Locals.BodyCharacterBuildOutputs.Contains(BodyPair.Key)
			? Locals.BodyCharacterBuildOutputs[BodyPair.Key].BodyMesh
			: nullptr;

		CrowdEditorUtilities::FMeshConstructionParams ActorBodyParams;
		ActorBodyParams.LODsToKeep = ActorBodySourceLODs;
		ActorBodyParams.TargetSkeleton = TargetSkeleton;
		ActorBodyParams.bPreserveDNA = (SourceDNAMesh != nullptr);
		ActorBodyParams.SourceDNAMesh = SourceDNAMesh;
		ActorBodyParams.ForceKeepBoneNames = BodyForceKeepBones;

		FMetaHumanCrowdCollectionBodyBuildOutput& BodyBuildOutput = BodyCollectionBuildOutputs.FindOrAdd(BodyPair.Key);
		BodyBuildOutput.ActorBodyMesh = CrowdEditorUtilities::ConstructMeshFromBundle(
			BodyPair.Value, ActorBodyParams, Params.OuterForGeneratedAssets);

		if (!BodyBuildOutput.ActorBodyMesh)
		{
			continue;
		}

		UE::MetaHuman::CrowdEditorUtilities::ApplyMinLODToMesh(
			BodyBuildOutput.ActorBodyMesh, ActorMeshMinLOD, ActorMeshQualityLevelMinLOD);

		if (SourceDNAMesh)
		{
			if (BodyCorrectivesLODThreshold >= 0)
			{
				BodyBuildOutput.ActorBodyMesh->SetPostProcessAnimBlueprint(SourceDNAMesh->GetPostProcessAnimBlueprint());
				BodyBuildOutput.ActorBodyMesh->SetPostProcessAnimGraphLODThreshold(BodyCorrectivesLODThreshold);
			}

			if (UPhysicsAsset* SourcePhysicsAsset = SourceDNAMesh->GetPhysicsAsset())
			{
				BodyBuildOutput.ActorBodyMesh->SetPhysicsAsset(SourcePhysicsAsset);
			}
		}
	}


	// -------------------------------------------------------------------------
	// Phase 5: Animation baking
	// -------------------------------------------------------------------------

	// Metadata produced during baking, accumulated per head item key and applied at Phase 6 integration.
	TMap<FMetaHumanPaletteItemKey, TArray<FMetaHumanGeneratedAssetMetadata>> HeadBakedMetadata;

	// The first root provider produced during baking, captured for use by non-face per-mesh ASTPDs
	// (body, outfit, groom). Each head has its own root provider with head-specific baked face
	// tracks, but non-face meshes don't evaluate those tracks (their bone maps don't include face
	// bones), so any head's root provider can be used as the Sequences/Layers source for them.
	UAnimSequenceTransformProviderData* AnyRootProvider = nullptr;

	if (AnimationConfig)
	{
		TObjectPtr<UAnimBoneCompressionSettings> BoneCompressionSettings = BoneCompressionSettingsOverride;

		if (!BoneCompressionSettings)
		{
			BoneCompressionSettings = FAnimationUtils::GetDefaultAnimationRecorderBoneCompressionSettings();
		}

		auto ApplyEntrySettings = [](const FMetaHumanCrowdBakeAnimationData& Entry, UAnimSequence* Anim)
		{
			Anim->bLoop = Entry.bLoop;
			Anim->bEnableRootMotion = Entry.bRootMotion;
			Anim->bForceRootLock = Entry.bRootMotion;
		};

		// Generate merged animation for each valid animation config entry
		Locals.AnimSequencePerEntry.Empty();
		TSet<FName> ProcessedAnimNames;

		for (int32 AnimEntryIndex = 0; AnimEntryIndex < AnimationConfig->AnimationsToBake.Num(); ++AnimEntryIndex)
		{
			const FMetaHumanCrowdBakeAnimationData& AnimEntry = AnimationConfig->AnimationsToBake[AnimEntryIndex];

			if (!AnimEntry.IsValid())
			{
				UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "Skipping AnimConfig invalid entry at index {AnimEntryIndex}.", AnimEntryIndex);
				continue;
			}

			bool bAnimNameExists = false;
			ProcessedAnimNames.Add(AnimEntry.Name, &bAnimNameExists);

			if (bAnimNameExists)
			{
				UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Found duplicate entry name in the AnimConfig at index {AnimEntryIndex}: {AnimName}", AnimEntryIndex, AnimEntry.Name);
				continue;
			}

			// Body animation merged with face curves, on TargetSkeleton. RigLogic baking
			// happens later, per-head, in the bake loop below.
			UAnimSequence* MergedAnimSequence = nullptr;
			const FString MergedAnimationAssetName = FString::Format(TEXT("AS_{0}"), { AnimEntry.Name.ToString() });

			if (AnimEntry.MergedAnimSequence)
			{
				// Merged animation has both curves and joint animation
				MergedAnimSequence = UE::MetaHuman::CrowdEditorUtilities::CopyAnimationCurves(
					AnimEntry.MergedAnimSequence,
					AnimEntry.MergedAnimSequence,
					TargetSkeleton,
					BoneCompressionSettings,
					MergedAnimationAssetName,
					Params.OuterForGeneratedAssets);
			}
			else if (AnimEntry.FaceAnimSequence && AnimEntry.BodyAnimSequence)
			{
				// We need to copy face animation curves to the body animation
				MergedAnimSequence = UE::MetaHuman::CrowdEditorUtilities::CopyAnimationCurves(
					AnimEntry.FaceAnimSequence,
					AnimEntry.BodyAnimSequence,
					TargetSkeleton,
					BoneCompressionSettings,
					MergedAnimationAssetName,
					Params.OuterForGeneratedAssets);
			}
			else if (AnimEntry.BodyAnimSequence)
			{
				// No face animation, using body animation
				MergedAnimSequence = DuplicateObject<UAnimSequence>(AnimEntry.BodyAnimSequence, Params.OuterForGeneratedAssets, FName(MergedAnimationAssetName));

				if (MergedAnimSequence)
				{
					MergedAnimSequence->SetSkeleton(TargetSkeleton);
				}
			}

			if (!MergedAnimSequence)
			{
				UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "Failed to produce merged animation for entry '{AnimName}'.", AnimEntry.Name);
				continue;
			}

			ApplyEntrySettings(AnimEntry, MergedAnimSequence);

			Locals.AnimSequencePerEntry.Add(AnimEntry.Name, FMetaHumanCrowdBakedAnimEntry{ AnimEntry, MergedAnimSequence });
			GetCollectionBuildOutput().SharedAnimBPAnimations.Add(AnimEntry.Name, MergedAnimSequence);

			// Register the shared merged animation in the collection-level metadata so it gets
			// saved with the collection. Re-fetch the entry rather than using the reference
			// captured at the top of BuildCollection: CollectionBuildData lives inside a TArray
			// (ItemBuiltData.SortedElements) and IntegrateItemBuiltData() calls during earlier
			// item loops can reallocate that array, invalidating the reference.
			if (FMetaHumanPipelineBuiltData* CollectionBuildDataEntry = BuiltData->PaletteBuiltData.ItemBuiltData.MutableView().Find(FMetaHumanPaletteItemPath::Collection))
			{
				const FString SharedAnimAssetName = FString::Format(TEXT("AS_{0}"), { AnimEntry.Name.ToString() });
				CollectionBuildDataEntry->Metadata.Emplace(
					MergedAnimSequence,
					TEXT("Animations"),
					SharedAnimAssetName);
			}
		}

		for (TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdGroomFitTarget>& HeadGroomFitTarget : Locals.HeadGroomFitTargets)
		{
			const FMetaHumanPaletteItemKey& HeadItemKey = HeadGroomFitTarget.Key;
			FMetaHumanCrowdMeshGeometryBundle* FaceBundle = FaceGeometryBundles.Find(HeadItemKey);
			if (!FaceBundle)
			{
				continue;
			}

			FMetaHumanCrowdCollectionHeadBuildOutput* HeadBuildOutput = HeadCollectionBuildOutputs.Find(HeadItemKey);
			if (!HeadBuildOutput)
			{
				continue;
			}

			// Build a transient face mesh from the geometry bundle for animation baking.
			// Uses instanced LODs since baked animations play on ISKMs.
			// Needs DNA for RigLogic evaluation in the post-process ABP.
			USkeletalMesh* SourceDNAMesh = HeadGroomFitTarget.Value.TargetMesh;

			CrowdEditorUtilities::FMeshConstructionParams BakeMeshParams;
			BakeMeshParams.LODsToKeep = InstancedFaceSourceLODs;
			BakeMeshParams.TargetSkeleton = TargetSkeleton;
			BakeMeshParams.bPreserveDNA = (SourceDNAMesh != nullptr);
			BakeMeshParams.SourceDNAMesh = SourceDNAMesh;

			if (const FMetaHumanPaletteItemKey* CompatibleBodyKey = CompatibleBodyMap.Find(HeadItemKey))
			{
				if (const FMetaHumanCrowdMeshGeometryBundle* BodyBundle = BodyGeometryBundles.Find(*CompatibleBodyKey))
				{
					BakeMeshParams.AlignmentRefSkeleton = &BodyBundle->RefSkeleton;
					BakeMeshParams.AlignmentBoneName = HeadAlignmentBoneName;
				}
			}

			USkeletalMesh* TransientBakeMesh = CrowdEditorUtilities::ConstructMeshFromBundle(
				*FaceBundle, BakeMeshParams, GetTransientPackage());

			if (!TransientBakeMesh)
			{
				continue;
			}

			// Propagate the source mesh's PostProcess Anim Blueprint so RigLogic runs during
			// the bake and ctrl_expressions_* curves drive FACIAL_* bones (eye blinks etc.).
			// ConstructMeshFromBundle builds a fresh skeletal mesh and doesn't carry this over.
			if (SourceDNAMesh)
			{
				TransientBakeMesh->SetPostProcessAnimBlueprint(SourceDNAMesh->GetPostProcessAnimBlueprint());
				TransientBakeMesh->SetPostProcessAnimGraphLODThreshold(SourceDNAMesh->GetPostProcessAnimGraphLODThreshold());
			}

			const FString HeadCharacterName = HeadGroomFitTarget.Value.OptionalCharacter
				? HeadGroomFitTarget.Value.OptionalCharacter->GetName()
				: TransientBakeMesh->GetName();

			// TODO: No API currently available to set this directly, so using reflection here
			FArrayProperty* SequencesProp = CastField<FArrayProperty>(UAnimSequenceTransformProviderData::StaticClass()->FindPropertyByName(TEXT("Sequences")));
			if (!ensure(SequencesProp))
			{
				continue;
			}
			FStructProperty* SeqStructProp = CastField<FStructProperty>(SequencesProp->Inner);
			if (!ensure(SeqStructProp))
			{
				continue;
			}
			FObjectPropertyBase* AnimSeqProp = CastField<FObjectPropertyBase>(SeqStructProp->Struct->FindPropertyByName(TEXT("Sequence")));
			if (!ensure(AnimSeqProp))
			{
				continue;
			}
			FArrayProperty* LayersProp = CastField<FArrayProperty>(UAnimSequenceTransformProviderData::StaticClass()->FindPropertyByName(TEXT("Layers")));
			if (!ensure(LayersProp))
			{
				continue;
			}

			TArray<TObjectPtr<UAnimSequence>> BakedSequences;

			// The ASTPD Sequences array (and hence BakedSequences, which is used to populate it) 
			// must end up in the same order as AnimationConfig->AnimationsToBake, because indices
			// from AnimationsToBake are used to determine which animation index to play from 
			// Sequences.
			//
			// Therefore, we iterate AnimationsToBake here rather than AnimSequencePerEntry to 
			// ensure precise ordering.
			for (const FMetaHumanCrowdBakeAnimationData& AnimEntry : AnimationConfig->AnimationsToBake)
			{
				const FMetaHumanCrowdBakedAnimEntry* CachedEntry = Locals.AnimSequencePerEntry.Find(AnimEntry.Name);
				if (!CachedEntry)
				{
					// This can only happen if there was an error populating AnimSequencePerEntry, 
					// in which case the error will already have been logged.
					continue;
				}

				TObjectPtr<UAnimSequence> MergedAnimSequence = CachedEntry->AnimSequence;

				const FString BakedAnimationName = FString::Format(TEXT("AS_{0}_{1}_Baked"), { HeadCharacterName, AnimEntry.Name.ToString() });

				UAnimSequence* BakedAnimation = UE::MetaHuman::CrowdEditorUtilities::BakeAnimation(
					MergedAnimSequence,
					TransientBakeMesh,
					BoneCompressionSettings,
					BakedAnimationName,
					Params.OuterForGeneratedAssets,
					AnimationConfig->FaceRootBoneName);

				if (BakedAnimation)
				{
					ApplyEntrySettings(AnimEntry, BakedAnimation);

					BakedSequences.Add(BakedAnimation);
					HeadBakedMetadata.FindOrAdd(HeadItemKey).Emplace(
						BakedAnimation,
						TEXT("Animations"),
						BakedAnimationName);
				}
				else
				{
					UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "Failed to bake animation '{AnimName}' for '{CharacterName}'", AnimEntry.Name, HeadCharacterName);
				}
			}

			if (!BakedSequences.IsEmpty())
			{
				UAnimSequenceTransformProviderData* RootProvider = NewObject<UAnimSequenceTransformProviderData>(Params.OuterForGeneratedAssets, *HeadCharacterName, RF_Public);

				FScriptArrayHelper SequencesHelper(SequencesProp, SequencesProp->ContainerPtrToValuePtr<void>(RootProvider));
				for (UAnimSequence* Sequence : BakedSequences)
				{
					const int32 NewIdx = SequencesHelper.AddValue();
					AnimSeqProp->SetObjectPropertyValue_InContainer(SequencesHelper.GetRawPtr(NewIdx), Sequence);
				}

				FScriptArrayHelper LayersHelper(LayersProp, LayersProp->ContainerPtrToValuePtr<void>(RootProvider));
				LayersHelper.AddValue();

				HeadBuildOutput->BakedAnimRootProvider = RootProvider;
				HeadBakedMetadata.FindOrAdd(HeadItemKey).Emplace(RootProvider, TEXT("Animations"), HeadCharacterName);

				// Capture the first root provider for use by non-face meshes (body/outfit/groom).
				if (!AnyRootProvider)
				{
					AnyRootProvider = RootProvider;
				}
			}
		}
	}

	// -------------------------------------------------------------------------
	// Phase 5.5: Create per-mesh ASTPDs for all instanced meshes
	//
	// Each instanced mesh gets its own ASTPD with SkinnedAsset bound to that specific mesh,
	// sharing the root provider's Sequences/Layers. Creating these at build time (rather than
	// runtime as transient objects) allows the ASTPD DDC — specifically per-sequence bounding
	// boxes in FAnimSequenceTransformProviderCachedData — to go through the cook pipeline.
	//
	// Actor-variant meshes do not get ASTPDs (they use traditional anim BPs, not ISKM tracks).
	// -------------------------------------------------------------------------

	{
		using UE::MetaHuman::CrowdEditorPipelinePrivate::CreatePerMeshTransformProvider;

		// 5.5a. Head (face) meshes — each head uses its own root provider
		for (TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdCollectionHeadBuildOutput>& HeadPair : HeadCollectionBuildOutputs)
		{
			FMetaHumanCrowdCollectionHeadBuildOutput& HeadBuildOutput = HeadPair.Value;
			if (!HeadBuildOutput.InstancedFaceMesh || !HeadBuildOutput.BakedAnimRootProvider)
			{
				continue;
			}

			const FString FaceAssetName = FString::Printf(TEXT("%s_ASTPD_Face"), *HeadBuildOutput.InstancedFaceMesh->GetName());
			HeadBuildOutput.InstancedFaceMeshTransformProvider = CreatePerMeshTransformProvider(
				HeadBuildOutput.BakedAnimRootProvider,
				HeadBuildOutput.InstancedFaceMesh,
				FaceAssetName,
				Params.OuterForGeneratedAssets);

			if (HeadBuildOutput.InstancedFaceMeshTransformProvider)
			{
				HeadBakedMetadata.FindOrAdd(HeadPair.Key).Emplace(HeadBuildOutput.InstancedFaceMeshTransformProvider, TEXT("Animations"), FaceAssetName);
			}
		}

		// 5.5b. Body meshes — use AnyRootProvider (body mesh bone maps don't include face bones,
		// so head-specific baked face tracks in the root provider are harmlessly ignored).
		if (AnyRootProvider)
		{
			for (TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdCollectionBodyBuildOutput>& BodyPair : BodyCollectionBuildOutputs)
			{
				FMetaHumanCrowdCollectionBodyBuildOutput& BodyBuildOutput = BodyPair.Value;
				if (!BodyBuildOutput.InstancedBodyMesh)
				{
					continue;
				}

				const FString BodyAssetName = FString::Printf(TEXT("%s_ASTPD_Body"), *BodyBuildOutput.InstancedBodyMesh->GetName());
				BodyBuildOutput.InstancedBodyMeshTransformProvider = CreatePerMeshTransformProvider(
					AnyRootProvider,
					BodyBuildOutput.InstancedBodyMesh,
					BodyAssetName,
					Params.OuterForGeneratedAssets);
			}
		}

		// 5.5c. Outfit meshes — walk the integrated outfit build outputs and populate the parallel
		// transform-provider maps alongside the existing instanced mesh maps.
		if (AnyRootProvider)
		{
			for (const FMetaHumanPipelineBuiltDataCollectionPair& Pair : BuiltData->PaletteBuiltData.ItemBuiltData.View().SortedElements)
			{
				if (!Pair.Value.BuildOutput.GetPtr<FMetaHumanCrowdCollectionOutfitBuildOutput>())
				{
					continue;
				}

				FMetaHumanPipelineBuiltData* MutableBuiltData = BuiltData->PaletteBuiltData.ItemBuiltData.MutableView().Find(Pair.Key);
				if (!MutableBuiltData)
				{
					continue;
				}
				FMetaHumanCrowdCollectionOutfitBuildOutput* OutfitOutput = MutableBuiltData->BuildOutput.GetMutablePtr<FMetaHumanCrowdCollectionOutfitBuildOutput>();
				if (!OutfitOutput)
				{
					continue;
				}

				for (const TPair<FMetaHumanPaletteItemKey, TObjectPtr<USkeletalMesh>>& MeshPair : OutfitOutput->BodyToInstancedOutfitMeshMap)
				{
					if (!MeshPair.Value)
					{
						continue;
					}

					const FString OutfitAssetName = FString::Printf(TEXT("%s_ASTPD_Outfit"), *MeshPair.Value->GetName());
					UAnimSequenceTransformProviderData* OutfitProvider = CreatePerMeshTransformProvider(
						AnyRootProvider,
						MeshPair.Value,
						OutfitAssetName,
						Params.OuterForGeneratedAssets);

					if (OutfitProvider)
					{
						OutfitOutput->BodyToInstancedOutfitTransformProviderMap.Add(MeshPair.Key, OutfitProvider);
						MutableBuiltData->Metadata.Emplace(OutfitProvider, TEXT("Animations"), OutfitAssetName);
					}
				}
			}
		}

		// 5.5d. Groom meshes — find the matching head and use its root provider
		for (const FMetaHumanPipelineBuiltDataCollectionPair& Pair : BuiltData->PaletteBuiltData.ItemBuiltData.View().SortedElements)
		{
			if (!Pair.Value.BuildOutput.GetPtr<FMetaHumanCrowdCollectionGroomBuildOutput>())
			{
				continue;
			}

			FMetaHumanPipelineBuiltData* MutableBuiltData = BuiltData->PaletteBuiltData.ItemBuiltData.MutableView().Find(Pair.Key);
			if (!MutableBuiltData)
			{
				continue;
			}
			FMetaHumanCrowdCollectionGroomBuildOutput* GroomOutput = MutableBuiltData->BuildOutput.GetMutablePtr<FMetaHumanCrowdCollectionGroomBuildOutput>();
			if (!GroomOutput)
			{
				continue;
			}

			for (const TPair<FMetaHumanPaletteItemKey, TObjectPtr<USkeletalMesh>>& MeshPair : GroomOutput->ItemToInstancedGroomMeshMap)
			{
				if (!MeshPair.Value)
				{
					continue;
				}

				const FMetaHumanCrowdCollectionHeadBuildOutput* HeadOutput = HeadCollectionBuildOutputs.Find(MeshPair.Key);
				if (!HeadOutput || !HeadOutput->BakedAnimRootProvider)
				{
					continue;
				}

				const FString GroomAssetName = FString::Printf(TEXT("%s_ASTPD_Groom"), *MeshPair.Value->GetName());
				UAnimSequenceTransformProviderData* GroomProvider = CreatePerMeshTransformProvider(
					HeadOutput->BakedAnimRootProvider,
					MeshPair.Value,
					GroomAssetName,
					Params.OuterForGeneratedAssets);

				if (GroomProvider)
				{
					GroomOutput->ItemToInstancedGroomTransformProviderMap.Add(MeshPair.Key, GroomProvider);
					MutableBuiltData->Metadata.Emplace(GroomProvider, TEXT("Animations"), GroomAssetName);
				}
			}
		}
	}

	// -------------------------------------------------------------------------
	// Phase 6: Integrate collection-level head and body build outputs
	// -------------------------------------------------------------------------

	for (TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdCollectionHeadBuildOutput>& HeadPair : HeadCollectionBuildOutputs)
	{
		const FMetaHumanPaletteItemPath HeadItemPath(HeadPair.Key);
		FMetaHumanPaletteBuiltData HeadBuiltData;
		FMetaHumanPipelineBuiltData& HeadPipelineBuiltData = HeadBuiltData.ItemBuiltData.Edit().Add(HeadItemPath);

		const FString HeadAssetName = HeadPair.Key.ToAssetNameString();
		HeadPipelineBuiltData.DefaultUnpackSubfolder = FString(TEXT("Heads")) / HeadAssetName;
		
		if (HeadPair.Value.ActorFaceMesh)
		{
			HeadPipelineBuiltData.Metadata.Emplace(HeadPair.Value.ActorFaceMesh, HeadPipelineBuiltData.DefaultUnpackSubfolder, FString::Format(TEXT("SKM_{0}_Actor"), { HeadAssetName }));
		}

		if (HeadPair.Value.InstancedFaceMesh)
		{
			HeadPipelineBuiltData.Metadata.Emplace(HeadPair.Value.InstancedFaceMesh, HeadPipelineBuiltData.DefaultUnpackSubfolder, FString::Format(TEXT("SKM_{0}_Inst"), { HeadAssetName }));
		}

		// Add animation metadata from phase 5
		if (TArray<FMetaHumanGeneratedAssetMetadata>* BakedMetadata = HeadBakedMetadata.Find(HeadPair.Key))
		{
			HeadPipelineBuiltData.Metadata.Append(MoveTemp(*BakedMetadata));
		}

		FMetaHumanCrowdCollectionHeadBuildOutput& HeadBuildOutput = HeadPipelineBuiltData.BuildOutput.InitializeAs<FMetaHumanCrowdCollectionHeadBuildOutput>(MoveTemp(HeadPair.Value));
		if (const FMetaHumanPaletteItemKey* CompatibleBodyKey = CompatibleBodyMap.Find(HeadPair.Key))
		{
			HeadBuildOutput.CompatibleBody = *CompatibleBodyKey;
		}

		BuiltData->PaletteBuiltData.IntegrateItemBuiltData(HeadItemPath, UMetaHumanCrowdPipeline::HeadSlotName, MoveTemp(HeadBuiltData));
	}

	for (TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdCollectionBodyBuildOutput>& BodyPair : BodyCollectionBuildOutputs)
	{
		const FMetaHumanPaletteItemPath BodyItemPath(BodyPair.Key);
		FMetaHumanPaletteBuiltData BodyBuiltData;
		FMetaHumanPipelineBuiltData& BodyPipelineBuiltData = BodyBuiltData.ItemBuiltData.Edit().Add(BodyItemPath);

		const FString BodyAssetName = BodyPair.Key.ToAssetNameString();
		BodyPipelineBuiltData.DefaultUnpackSubfolder = FString(TEXT("Bodies")) / BodyAssetName;
		
		if (BodyPair.Value.ActorBodyMesh)
		{
			BodyPipelineBuiltData.Metadata.Emplace(BodyPair.Value.ActorBodyMesh, BodyPipelineBuiltData.DefaultUnpackSubfolder, FString::Format(TEXT("SKM_{0}_Actor"), { BodyAssetName }));
		}
		
		if (BodyPair.Value.InstancedBodyMesh)
		{
			BodyPipelineBuiltData.Metadata.Emplace(BodyPair.Value.InstancedBodyMesh, BodyPipelineBuiltData.DefaultUnpackSubfolder, FString::Format(TEXT("SKM_{0}_Inst"), { BodyAssetName }));
		}

		BodyPipelineBuiltData.BuildOutput.InitializeAs<FMetaHumanCrowdCollectionBodyBuildOutput>(MoveTemp(BodyPair.Value));
		BuiltData->PaletteBuiltData.IntegrateItemBuiltData(BodyItemPath, UMetaHumanCrowdPipeline::BodySlotName, MoveTemp(BodyBuiltData));
	}

	TSharedRef<FMetaHumanCollectionBuiltData> BuildDataShared = MakeShared<FMetaHumanCollectionBuiltData>();
	*BuildDataShared = MoveTemp(*BuiltData);
	OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Succeeded, BuildDataShared);
}

EMetaHumanWardrobeItemCompatibility UMetaHumanCrowdEditorPipeline::TestWardrobeItemCompatibilityWithSlot(FName SlotName, TNotNull<const UMetaHumanWardrobeItem*> WardrobeItem) const
{
	// Support the standard groom and clothing pipelines via the import path

	const UMetaHumanItemPipeline* ItemPipeline = WardrobeItem->GetPipeline();
	if (ItemPipeline)
	{
		const UMetaHumanCharacterPipelineSpecification* Spec = GetRuntimeCharacterPipeline()->GetSpecification();

		if (ItemPipeline->IsA<UMetaHumanGroomPipeline>()
			&& UE::MetaHuman::CrowdEditorPipelinePrivate::IsGroomSlot(Spec, SlotName))
		{
			return EMetaHumanWardrobeItemCompatibility::Import;
		}

		if (ItemPipeline->IsA<UMetaHumanOutfitPipeline>()
			&& UE::MetaHuman::CrowdEditorPipelinePrivate::IsOutfitSlot(Spec, SlotName))
		{
			return EMetaHumanWardrobeItemCompatibility::Import;
		}

		if (ItemPipeline->IsA<UMetaHumanSkeletalMeshPipeline>()
			&& UE::MetaHuman::CrowdEditorPipelinePrivate::IsOutfitSlot(Spec, SlotName))
		{
			return EMetaHumanWardrobeItemCompatibility::Import;
		}
	}

	return Super::TestWardrobeItemCompatibilityWithSlot(SlotName, WardrobeItem);
}

bool UMetaHumanCrowdEditorPipeline::TryCreateItemForImport(
	TNotNull<UMetaHumanCollection*> Collection,
	FName SlotName,
	TNotNull<const UMetaHumanWardrobeItem*> SourceWardrobeItem,
	FMetaHumanCharacterPaletteItem& OutItem)
{
	const UMetaHumanItemPipeline* SourcePipeline = SourceWardrobeItem->GetPipeline();
	if (!SourcePipeline)
	{
		return false;
	}

	const UMetaHumanCharacterPipelineSpecification* Spec = GetRuntimeCharacterPipeline()->GetSpecification();

	UMetaHumanWardrobeItem* NewWardrobeItem = NewObject<UMetaHumanWardrobeItem>(Collection);
	NewWardrobeItem->PrincipalAsset = SourceWardrobeItem->PrincipalAsset;

	if (SourcePipeline->IsA<UMetaHumanGroomPipeline>()
		&& UE::MetaHuman::CrowdEditorPipelinePrivate::IsGroomSlot(Spec, SlotName))
	{
		if (!GroomPipelineClass)
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Error,
				"Crowd pipeline {Pipeline} has no Groom Pipeline Class set; cannot import groom item.",
				GetPathName());
			return false;
		}

		UMetaHumanCrowdGroomPipeline* NewPipeline = NewObject<UMetaHumanCrowdGroomPipeline>(NewWardrobeItem, GroomPipelineClass);
		NewPipeline->SetDefaultEditorPipeline();
		NewPipeline->SourceGroomItem = SourceWardrobeItem;
		NewWardrobeItem->SetPipeline(NewPipeline);
	}
	else if (SourcePipeline->IsA<UMetaHumanOutfitPipeline>()
		&& UE::MetaHuman::CrowdEditorPipelinePrivate::IsOutfitSlot(Spec, SlotName))
	{
		if (!OutfitPipelineClass)
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Error,
				"Crowd pipeline {Pipeline} has no Outfit Pipeline Class set; cannot import outfit item.",
				GetPathName());
			return false;
		}

		UMetaHumanCrowdOutfitPipeline* NewPipeline = NewObject<UMetaHumanCrowdOutfitPipeline>(NewWardrobeItem, OutfitPipelineClass);
		NewPipeline->SetDefaultEditorPipeline();
		NewPipeline->SourceOutfitItem = SourceWardrobeItem;
		NewWardrobeItem->SetPipeline(NewPipeline);
	}
	else if (SourcePipeline->IsA<UMetaHumanSkeletalMeshPipeline>()
		&& UE::MetaHuman::CrowdEditorPipelinePrivate::IsOutfitSlot(Spec, SlotName))
	{
		if (!SkeletalClothingPipelineClass)
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Error,
				"Crowd pipeline {Pipeline} has no Skeletal Clothing Pipeline Class set; cannot import skeletal mesh clothing item.",
				GetPathName());
			return false;
		}

		UMetaHumanCrowdSkeletalClothingPipeline* NewPipeline = NewObject<UMetaHumanCrowdSkeletalClothingPipeline>(NewWardrobeItem, SkeletalClothingPipelineClass);
		NewPipeline->SetDefaultEditorPipeline();
		NewPipeline->SourceSkeletalClothingItem = SourceWardrobeItem;
		NewWardrobeItem->SetPipeline(NewPipeline);
	}
	else
	{
		return false;
	}

	OutItem.WardrobeItem = NewWardrobeItem;
	OutItem.SlotName = SlotName;

	return true;
}

bool UMetaHumanCrowdEditorPipeline::CanBuild() const
{
	return true;
}

bool UMetaHumanCrowdEditorPipeline::ValidateCollection(TNotNull<UMetaHumanCollection*> Collection)
{
	if (!Super::ValidateCollection(Collection))
	{
		return false;
	}

	if (!TargetSkeleton)
	{
		if (FApp::IsUnattended())
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Crowd pipeline {Pipeline} has no Target Skeleton set", GetPathName());
			return false;
		}

		USkeleton* Template = TargetSkeletonTemplate.LoadSynchronous();
		if (!Template)
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Error,
				"Crowd pipeline {Pipeline} has no Target Skeleton set, and the Target Skeleton Template {Template} could not be loaded.",
				GetPathName(), TargetSkeletonTemplate.ToString());
			return false;
		}

		const FString CollectionFolder = FPaths::GetPath(Collection->GetPathName());

		USkeleton* ChosenSkeleton = nullptr;
		if (!UE::MetaHuman::CrowdEditor::PromptForTargetSkeleton(Template, CollectionFolder, ChosenSkeleton)
			|| !ChosenSkeleton)
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning,
				"Crowd pipeline {Pipeline} build aborted: user did not choose a Target Skeleton.",
				GetPathName());
			return false;
		}

		TargetSkeleton = ChosenSkeleton;
		MarkPackageDirty();

		UE_LOGFMT(LogMetaHumanCrowdEditor, Log,
			"Crowd pipeline {Pipeline} Target Skeleton set to {Skeleton} from user prompt.",
			GetPathName(), ChosenSkeleton->GetPathName());
	}

	// Ensure TargetSkeleton is marked compatible with the skeletons of any head or body meshes used
	{
		TArray<USkeleton*> CandidateSkeletons;

		auto AddSkeletonFromMesh = [&CandidateSkeletons](USkeletalMesh* Mesh)
		{
			if (Mesh)
			{
				if (USkeleton* MeshSkeleton = Mesh->GetSkeleton())
				{
					CandidateSkeletons.AddUnique(MeshSkeleton);
				}
			}
		};

		for (const FMetaHumanCharacterPaletteItem& Item : Collection->GetItems())
		{
			if (Item.SlotName != UMetaHumanCrowdPipeline::HeadSlotName
				&& Item.SlotName != UMetaHumanCrowdPipeline::BodySlotName)
			{
				continue;
			}

			const FMetaHumanPaletteItemPath ItemPath(Item.GetItemKey());

			const UMetaHumanItemPipeline* ItemPipeline = nullptr;
			static_cast<void>(Collection->TryResolveItemPipeline(ItemPath, ItemPipeline));
			if (!ItemPipeline)
			{
				continue;
			}

			const UMetaHumanCrowdCharacterEditorPipeline* CharacterEditorPipeline =
				Cast<UMetaHumanCrowdCharacterEditorPipeline>(ItemPipeline->GetEditorPipeline());
			if (!CharacterEditorPipeline)
			{
				continue;
			}

			AddSkeletonFromMesh(CharacterEditorPipeline->FaceMesh);
			AddSkeletonFromMesh(CharacterEditorPipeline->BodyMesh);
			AddSkeletonFromMesh(CharacterEditorPipeline->MergedHeadAndBodyMesh);
		}

		if (!UE::MetaHuman::CrowdEditor::PromptToAddCompatibleSkeletons(Collection, TargetSkeleton, CandidateSkeletons))
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning,
				"Crowd pipeline {Pipeline} build aborted: user did not approve adding "
				"compatible skeletons to {Skeleton}.",
				GetPathName(), TargetSkeleton->GetPathName());
			return false;
		}
	}

	return true;
}

bool UMetaHumanCrowdEditorPipeline::TryUnpackInstanceAssets(
	TNotNull<UMetaHumanInstance*> Instance,
	FInstancedStruct& AssemblyOutput,
	TArray<FMetaHumanGeneratedAssetMetadata>& AssemblyAssetMetadata,
	const FString& TargetFolder) const
{
	return false;
}

TSubclassOf<AActor> UMetaHumanCrowdEditorPipeline::GetEditorActorClass() const
{
	return AMetaHumanCharacterEditorActor::StaticClass();
}

TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> UMetaHumanCrowdEditorPipeline::GetSpecification() const
{
	return Specification;
}
