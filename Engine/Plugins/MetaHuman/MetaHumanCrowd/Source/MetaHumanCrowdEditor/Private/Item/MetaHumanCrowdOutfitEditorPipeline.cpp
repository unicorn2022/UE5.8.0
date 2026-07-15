// Copyright Epic Games, Inc. All Rights Reserved.

#include "Item/MetaHumanCrowdOutfitEditorPipeline.h"

#include "Item/MetaHumanCrowdOutfitPipeline.h"
#include "Item/MetaHumanOutfitPipeline.h"
#include "Item/MetaHumanOutfitEditorPipeline.h"
#include "MetaHumanCrowdEditorLog.h"
#include "MetaHumanCrowdTypes.h"
#include "Logging/StructuredLog.h"
#include "MetaHumanCrowdEditorUtilities.h"
#include "MetaHumanHashUtilities.h"
#include "MetaHumanWardrobeItem.h"

#include "ChaosOutfitAsset/OutfitAsset.h"
#include "Dataflow/DataflowObject.h"
#include "Engine/SkeletalMesh.h"

#include "DerivedDataCache.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataRequestOwner.h"
#include "DerivedDataRequestTypes.h"
#include "DerivedDataValueId.h"
#include "Engine/Texture2D.h"
#include "Serialization/MemoryReader.h"
#include "IO/IoHash.h"
#include "MeshDescription.h"
#include "UObject/Package.h"

namespace UE::MetaHuman::Private
{
	static const FName OutfitResize_TargetBodyPropertyName("TargetBody");
	static const FName OutfitResize_ResizableOutfitPropertyName("ResizableOutfit");

	// Bump this GUID whenever the outfit fitting logic changes in a way that invalidates cached results.
	static const FGuid OutfitFittingDDCVersion(0xB1D574F5, 0x34D7427E, 0xA64C03AB, 0x95FD130F);

	static UE::DerivedData::FCacheKey MakeOutfitFittingCacheKey(
		const FString& TargetMeshDerivedDataKey,
		const UObject* OutfitAsset,
		const FIoHash& DataflowPackageSavedHash,
		const FGuid& BuildCacheGuid,
		bool bLogDetails = false)
	{
		using namespace UE::DerivedData;

		FIoHashBuilder KeyHashBuilder;
		KeyHashBuilder.Update(&OutfitFittingDDCVersion, sizeof(OutfitFittingDDCVersion));
		KeyHashBuilder.Update(TargetMeshDerivedDataKey.GetCharArray().GetData(), TargetMeshDerivedDataKey.GetCharArray().Num() * sizeof(TCHAR));

		// Outfit asset
		bool bOutfitHashIsSavedHash;
		FIoHash OutfitHash;
		{
			const UPackage* OutfitAssetPackage = OutfitAsset->GetPackage();
			check(OutfitAssetPackage);
			
			bOutfitHashIsSavedHash = !OutfitAssetPackage->IsDirty();
			if (bOutfitHashIsSavedHash)
			{
				// Since the outfit is unmodified, we can use the hash that was calculated when it was saved
				OutfitHash = OutfitAssetPackage->GetSavedHash();
			}
			else
			{
				// Outfit is modified in memory, so hash the contents in memory.
				//
				// This takes much longer than using the saved hash, but is still faster than
				// resizing the outfit.
				OutfitHash = HashUtilities::HashUObject(OutfitAsset, bLogDetails);
			}

			KeyHashBuilder.Update(&bOutfitHashIsSavedHash, sizeof(bOutfitHashIsSavedHash));
			KeyHashBuilder.Update(&OutfitHash, sizeof(OutfitHash));
		}

		// Use the package saved hash for the dataflow asset instead of hashing its contents,
		// because the dataflow serialization is non-deterministic across loads.
		KeyHashBuilder.Update(&DataflowPackageSavedHash, sizeof(DataflowPackageSavedHash));

		KeyHashBuilder.Update(&BuildCacheGuid, sizeof(BuildCacheGuid));

		FCacheKey CacheKey;
		CacheKey.Bucket = FCacheBucket(TEXT("MetaHumanOutfitFitting"));
		CacheKey.Hash = KeyHashBuilder.Finalize();

		if (bLogDetails)
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "MakeOutfitFittingCacheKey inputs:");
			UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  TargetMeshDerivedDataKey: {TargetMeshDerivedDataKey}", TargetMeshDerivedDataKey);
			UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  OutfitAsset: {OutfitAsset}", OutfitAsset ? OutfitAsset->GetPathName() : FString(TEXT("null")));
			UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  DataflowPackageSavedHash: {DataflowPackageSavedHash}", LexToString(DataflowPackageSavedHash));
			UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  BuildCacheGuid: {BuildCacheGuid}", BuildCacheGuid.ToString());
			UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  OutfitFittingDDCVersion: {OutfitFittingDDCVersion}", OutfitFittingDDCVersion.ToString());
			UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  bOutfitHashIsSavedHash: {bOutfitHashIsSavedHash}", LexToString(bOutfitHashIsSavedHash));
			UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  OutfitHash: {OutfitHash}", LexToString(OutfitHash));
			UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "  Final CacheKey Hash: {CacheKeyHash}", LexToString(CacheKey.Hash));
		}

		return CacheKey;
	}

	static const UE::DerivedData::FValueId OutfitMeshDescriptionsValueId = UE::DerivedData::FValueId::FromName("OutfitMeshDescriptions");

	static FString MakeOutfitFittingCacheRecordName(const USkeletalMesh* TargetBodyMesh)
	{
		return FString::Format(TEXT("MetaHumanCrowdOutfitFitting_{0}"), { TargetBodyMesh->GetPathName() });
	}

	struct FFittedOutfitMaterialMapping
	{
		// For each slot on the fitted outfit mesh, this stores the index of the corresponding slot 
		// on the source outfit asset.
		TArray<int32> MaterialIndexMapping;

		// The names of the material slots on the fitted mesh.
		TArray<FName> MaterialSlotNames;
	};

	static FFittedOutfitMaterialMapping GenerateFittedOutfitMaterialMapping(
		TNotNull<const UChaosOutfitAsset*> OutfitAsset,
		TNotNull<const USkeletalMesh*> FittedOutfitMesh)
	{
		FFittedOutfitMaterialMapping Result;

		const TArray<FSkeletalMaterial>& AssetMaterials = OutfitAsset->GetMaterials();
		const TArray<FSkeletalMaterial>& FittedMaterials = FittedOutfitMesh->GetMaterials();

		Result.MaterialIndexMapping.Reserve(FittedMaterials.Num());
		Result.MaterialSlotNames.Reserve(FittedMaterials.Num());
		for (int32 FittedIdx = 0; FittedIdx < FittedMaterials.Num(); ++FittedIdx)
		{
			int32 MappedIndex = INDEX_NONE;
			for (int32 AssetIdx = 0; AssetIdx < AssetMaterials.Num(); ++AssetIdx)
			{
				if (AssetMaterials[AssetIdx].MaterialInterface == FittedMaterials[FittedIdx].MaterialInterface)
				{
					MappedIndex = AssetIdx;
					break;
				}
			}
			Result.MaterialIndexMapping.Add(MappedIndex);
			Result.MaterialSlotNames.Add(FittedMaterials[FittedIdx].MaterialSlotName);
		}

		return Result;
	}

	static void SerializeOutfitMeshDescriptions(
		FArchive& Ar,
		TArray<FMeshDescription>& MeshDescriptions,
		FReferenceSkeleton& RefSkeleton,
		FFittedOutfitMaterialMapping& FittedOutfitMaterialMapping)
	{
		Ar << RefSkeleton;

		int32 NumLODs = MeshDescriptions.Num();
		Ar << NumLODs;

		if (Ar.IsLoading())
		{
			MeshDescriptions.SetNum(NumLODs);
		}

		for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
		{
			Ar << MeshDescriptions[LODIndex];
		}

		Ar << FittedOutfitMaterialMapping.MaterialIndexMapping;
		Ar << FittedOutfitMaterialMapping.MaterialSlotNames;
	}

	// Serializes the MeshDescriptions and RefSkeleton from a fitted outfit into the DDC.
	// The RefSkeleton is cached because the fitted outfit's bone poses may differ from the
	// original OutfitAsset's RefSkeleton (different character proportions after fitting).
	static void StoreOutfitBundleInDDC(
		const UE::DerivedData::FCacheKey& CacheKey,
		TArray<FMeshDescription>& MeshDescriptions,
		FReferenceSkeleton& RefSkeleton,
		const USkeletalMesh* TargetBodyMesh,
		FFittedOutfitMaterialMapping& FittedOutfitMaterialMapping)
	{
		using namespace UE::DerivedData;

		ICache* Cache = TryGetCache();
		if (!Cache)
		{
			return;
		}

		TArray<uint8> SerializedData;
		FMemoryWriter Ar(SerializedData, /*bIsPersistent=*/ true);

		SerializeOutfitMeshDescriptions(Ar, MeshDescriptions, RefSkeleton, FittedOutfitMaterialMapping);

		FCacheRecordBuilder RecordBuilder(CacheKey);
		FValue Value = FValue::Compress(FSharedBuffer::MakeView(SerializedData.GetData(), SerializedData.Num()));
		RecordBuilder.AddValue(OutfitMeshDescriptionsValueId, MoveTemp(Value));

		FRequestOwner RequestOwner(EPriority::Normal);
		FCachePutRequest PutRequest = {
			FSharedString(MakeOutfitFittingCacheRecordName(TargetBodyMesh)),
			RecordBuilder.Build(),
			ECachePolicy::Default
		};
		Cache->Put(MakeArrayView(&PutRequest, 1), RequestOwner, [](FCachePutResponse&&) {});
		RequestOwner.Wait();
	}

	// Attempts to load cached MeshDescriptions and RefSkeleton from the DDC and populate a
	// geometry bundle. Materials are taken from the source OutfitAsset; the RefSkeleton comes
	// from the cache since the fitting process may adjust bone poses to match the target body.
	static bool TryLoadOutfitBundleFromDDC(
		const UE::DerivedData::FCacheKey& CacheKey,
		const UChaosOutfitAsset* OutfitAsset,
		const USkeletalMesh* TargetBodyMesh,
		FMetaHumanCrowdMeshGeometryBundle& OutBundle)
	{
		using namespace UE::DerivedData;

		ICache* Cache = TryGetCache();
		if (!Cache)
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "TryLoadOutfitBundleFromDDC: Failed - DDC cache interface not available (TryGetCache returned null)");
			return false;
		}

		FSharedBuffer CachedData;
		EStatus ResponseStatus = EStatus::Error;
		FCacheGetRequest Request;
		Request.Name = MakeOutfitFittingCacheRecordName(TargetBodyMesh);
		Request.Key = CacheKey;
		Request.Policy = ECachePolicy::Default;

		FRequestOwner RequestOwner(EPriority::Blocking);
		Cache->Get(MakeArrayView(&Request, 1), RequestOwner,
			[&CachedData, &ResponseStatus](FCacheGetResponse&& Response)
			{
				ResponseStatus = Response.Status;
				if (Response.Status == EStatus::Ok)
				{
					const FCompressedBuffer& CompressedBuffer = Response.Record.GetValue(OutfitMeshDescriptionsValueId).GetData();
					CachedData = CompressedBuffer.Decompress();
				}
			});
		RequestOwner.Wait();

		if (CachedData.IsNull())
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Warning, "TryLoadOutfitBundleFromDDC: Failed - DDC Get returned no data (Status={Status}) for record '{RecordName}', key hash {KeyHash}",
				static_cast<int32>(ResponseStatus), Request.Name, LexToString(CacheKey.Hash));

			return false;
		}

		FMemoryReaderView Ar(CachedData.GetView(), /*bIsPersistent=*/ true);

		FFittedOutfitMaterialMapping FittedOutfitMaterialMapping;
		SerializeOutfitMeshDescriptions(Ar, OutBundle.MeshDescriptions, OutBundle.RefSkeleton, FittedOutfitMaterialMapping);

		// Reconstruct the fitted mesh's material list from the cached index mapping.
		const TArray<FSkeletalMaterial>& AssetMaterials = OutfitAsset->GetMaterials();
		TArray<FSkeletalMaterial> FittedMaterials;
		FittedMaterials.Reserve(FittedOutfitMaterialMapping.MaterialIndexMapping.Num());
		for (int32 MappingIdx = 0; MappingIdx < FittedOutfitMaterialMapping.MaterialIndexMapping.Num(); ++MappingIdx)
		{
			const int32 AssetIndex = FittedOutfitMaterialMapping.MaterialIndexMapping[MappingIdx];
			if (!AssetMaterials.IsValidIndex(AssetIndex))
			{
				UE_LOGFMT(LogMetaHumanCrowdEditor, Error,
					"TryLoadOutfitMeshFromDDC: Cached material index {Index} out of range (asset has {Count} materials) - cache is stale",
					AssetIndex, AssetMaterials.Num());
				return false;
			}

			const FName ExpectedSlotName = FittedOutfitMaterialMapping.MaterialSlotNames.IsValidIndex(MappingIdx)
				? FittedOutfitMaterialMapping.MaterialSlotNames[MappingIdx]
				: NAME_None;
			const FName ActualSlotName = AssetMaterials[AssetIndex].MaterialSlotName;
			if (ExpectedSlotName != ActualSlotName)
			{
				UE_LOGFMT(LogMetaHumanCrowdEditor, Error,
					"TryLoadOutfitMeshFromDDC: Material slot name mismatch at index {Index}: cached={Cached}, asset={Asset} - cache is stale",
					AssetIndex, ExpectedSlotName, ActualSlotName);
				return false;
			}

			FittedMaterials.Add(AssetMaterials[AssetIndex]);
		}

		OutBundle.Materials = FittedMaterials;

		return true;
	}
}

UMetaHumanCrowdOutfitEditorPipeline::UMetaHumanCrowdOutfitEditorPipeline()
{
	Specification = CreateDefaultSubobject<UMetaHumanCharacterEditorPipelineSpecification>("Specification");
	Specification->BuildInputStruct = FMetaHumanCrowdOutfitBuildInput::StaticStruct();
}

UE::Tasks::TTask<FMetaHumanPaletteBuiltData> UMetaHumanCrowdOutfitEditorPipeline::BuildItem(const FBuildItemParams& Params) const
{
	const UMetaHumanCrowdOutfitPipeline* RuntimePipeline = Cast<UMetaHumanCrowdOutfitPipeline>(GetRuntimePipeline());
	if (!RuntimePipeline)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Runtime pipeline for item {ItemPath} must be a UMetaHumanCrowdOutfitPipeline", Params.ItemPath.ToDebugString());
		
		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	if (!Params.BuildInput.GetPtr<FMetaHumanCrowdOutfitBuildInput>())
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Build input not provided to Crowd Outfit pipeline during build");
		
		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	const UObject* LoadedAsset = Params.WardrobeItem->PrincipalAsset.LoadSynchronous();
	const UChaosOutfitAsset* OutfitAsset = Cast<UChaosOutfitAsset>(LoadedAsset);
	if (!OutfitAsset)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Crowd Outfit pipeline failed to load Outfit {Outfit} during build", Params.WardrobeItem->PrincipalAsset.ToString());
		
		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	const FMetaHumanCrowdOutfitBuildInput& OutfitBuildInput = Params.BuildInput.Get<FMetaHumanCrowdOutfitBuildInput>();

	FMetaHumanPaletteBuiltData BuiltDataResult;
	FMetaHumanPipelineBuiltData& OutfitBuiltData = BuiltDataResult.ItemBuiltData.Edit().Add(Params.ItemPath);
	OutfitBuiltData.DefaultUnpackSubfolder = FString::Format(TEXT("Outfits/{0}"), { LoadedAsset->GetName() });

	FMetaHumanCrowdOutfitBuildOutput& OutfitBuildOutput = OutfitBuiltData.BuildOutput.InitializeAs<FMetaHumanCrowdOutfitBuildOutput>();

	// Fetch the body hidden face map from the source outfit's editor pipeline
	if (RuntimePipeline->SourceOutfitItem)
	{
		const UMetaHumanOutfitEditorPipeline* SourceEditorPipeline = Cast<UMetaHumanOutfitEditorPipeline>(
			RuntimePipeline->SourceOutfitItem->GetEditorPipeline());
		if (SourceEditorPipeline
			&& SourceEditorPipeline->BodyHiddenFaceMapTexture.Texture
			&& SourceEditorPipeline->BodyHiddenFaceMapTexture.Texture->Source.IsValid())
		{
			OutfitBuildOutput.BodyHiddenFaceMap = SourceEditorPipeline->BodyHiddenFaceMapTexture;
		}
	}

	UChaosOutfitAsset* FittedOutfit = NewObject<UChaosOutfitAsset>();
	FittedOutfit->SetDataflow(OutfitBuildInput.OutfitResizeDataflowAsset);

	for (const TPair<FMetaHumanPaletteItemKey, FMetaHumanCrowdOutfitFitTarget>& Pair : OutfitBuildInput.FitTargets)
	{
		if (CompatibleBodies.Num() > 0 && !CompatibleBodies.Contains(Pair.Value.BodyCharacter))
		{
			// This body isn't supported by this outfit
			continue;
		}

		// Build DDC key from: target mesh derived data key, outfit asset, dataflow package hash, and version GUID.
		//
		// The dataflow asset's serialization contains non-deterministic data that changes on
		// every load, so we cannot hash its contents directly. Instead we use the package's
		// saved hash which is a hash of its file contents. If the dataflow package has been
		// modified in memory, the saved hash no longer represents the current state, so we 
		// skip DDC entirely and always rebuild.
		const FString TargetMeshDerivedDataKey = Pair.Value.MergedHeadAndBodyMesh->GetDerivedDataKey();

		const UPackage* DataflowPackage = OutfitBuildInput.OutfitResizeDataflowAsset ? OutfitBuildInput.OutfitResizeDataflowAsset->GetPackage() : nullptr;
		const bool bDataflowPackageDirty = DataflowPackage && DataflowPackage->IsDirty();
		const FIoHash DataflowPackageSavedHash = DataflowPackage ? DataflowPackage->GetSavedHash() : FIoHash();
		const bool bUseDDC = !bDataflowPackageDirty;

		UE::DerivedData::FCacheKey CacheKey;
		if (bUseDDC)
		{
			CacheKey = UE::MetaHuman::Private::MakeOutfitFittingCacheKey(
				TargetMeshDerivedDataKey, OutfitAsset, DataflowPackageSavedHash, Params.BuildCacheGuid);
		}

		FMetaHumanCrowdMeshGeometryBundle OutfitBundle;
		bool bBundleReady = false;

		if (bDataflowPackageDirty)
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Log, "Dataflow package '{DataflowPackage}' is dirty - DDC disabled for this outfit fit",
				DataflowPackage ? DataflowPackage->GetName() : FString(TEXT("null")));
		}

		if (bUseDDC && UE::MetaHuman::Private::TryLoadOutfitBundleFromDDC(CacheKey, OutfitAsset, Pair.Value.MergedHeadAndBodyMesh, OutfitBundle))
		{
			bBundleReady = true;
		}

		if (!bBundleReady)
		{
			if (bUseDDC)
			{
				// Re-compute the cache key with logging to diagnose why the DDC missed
				UE::MetaHuman::Private::MakeOutfitFittingCacheKey(
					TargetMeshDerivedDataKey, OutfitAsset, DataflowPackageSavedHash, Params.BuildCacheGuid, /*bLogDetails=*/ true);
			}

			// DDC miss: generate the fitted outfit mesh from scratch

			// Generate fitted Outfit Asset
			{
				FDataflowVariableOverrides& FittedOutfitVariableOverrides = FittedOutfit->GetDataflowInstance().GetVariableOverrides();

				FittedOutfitVariableOverrides.OverrideVariableObject(UE::MetaHuman::Private::OutfitResize_TargetBodyPropertyName, Pair.Value.MergedHeadAndBodyMesh);
				FittedOutfitVariableOverrides.OverrideVariableObject(UE::MetaHuman::Private::OutfitResize_ResizableOutfitPropertyName, OutfitAsset);

				FittedOutfitVariableOverrides.OverrideVariableBool("TransferSkinWeights", true);
				FittedOutfitVariableOverrides.OverrideVariableBool("GenerateLOD0Only", false);
				FittedOutfitVariableOverrides.OverrideVariableBool("StripSimMesh", true);

				FittedOutfit->GetDataflowInstance().UpdateOwnerAsset(true);
			}

			// Convert fitted Outfit to a transient skeletal mesh so we can extract geometry.
			USkeletalMesh* TransientOutfitMesh = NewObject<USkeletalMesh>(GetTransientPackage());
			{
				UE::MetaHuman::CrowdEditorUtilities::FScopedSkeletalMeshChange ScopedSkeletalMeshChange(TransientOutfitMesh);

				if (!FittedOutfit->ExportToSkeletalMesh(*TransientOutfitMesh))
				{
					continue;
				}

				TransientOutfitMesh->SetSkeleton(Pair.Value.MergedHeadAndBodyMesh->GetSkeleton());
			}

			// Extract the geometry bundle from the transient mesh.
			// MeshDescriptions contain the fitted geometry before any post-processing.
			UE::MetaHuman::CrowdEditorUtilities::ExtractGeometryBundle(TransientOutfitMesh, OutfitBundle);

			// Cache the geometry for future runs.
			if (bUseDDC)
			{
				UE::MetaHuman::Private::FFittedOutfitMaterialMapping Mapping = UE::MetaHuman::Private::GenerateFittedOutfitMaterialMapping(OutfitAsset, TransientOutfitMesh);

				UE::MetaHuman::Private::StoreOutfitBundleInDDC(
					CacheKey, 
					OutfitBundle.MeshDescriptions, 
					OutfitBundle.RefSkeleton, 
					Pair.Value.MergedHeadAndBodyMesh,
					Mapping);
			}
		}

		OutfitBuildOutput.BodyToOutfitGeometryMap.Add(Pair.Key, MoveTemp(OutfitBundle));
	}

	// Generate Assembly Parameters
	if (RuntimePipeline->SourceOutfitItem)
	{
		if (const UMetaHumanOutfitPipeline* SourcePipeline = Cast<UMetaHumanOutfitPipeline>(RuntimePipeline->SourceOutfitItem->GetPipeline()))
		{
			const TArray<FSkeletalMaterial>& MaterialSections = OutfitAsset->GetMaterials();

			UE::MetaHuman::MaterialUtils::GenerateAssemblyParameters(
				SourcePipeline->OverrideMaterials,
				SourcePipeline->RuntimeMaterialParameters,
				MaterialSections.Num(),
				UE::MetaHuman::MaterialUtils::MakeFetchSlotNameDelegate(MaterialSections),
				UE::MetaHuman::MaterialUtils::MakeFetchSlotMaterialDelegate(MaterialSections),
				OutfitBuiltData.AssemblyParameters);
		}
	}

	return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>(MoveTemp(BuiltDataResult));
}

TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> UMetaHumanCrowdOutfitEditorPipeline::GetSpecification() const
{
	return Specification;
}
