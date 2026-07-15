// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/Utilities/WorldPartitionHLODUtilities.h"
#include "WorldPartition/WorldPartitionStreamingGeneration.h"

#if WITH_EDITOR

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/ContentBundle/ContentBundleActivationScope.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/HLOD/CustomHLODActor.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODActorExternalResources.h"
#include "WorldPartition/HLOD/HLODCreationFilter.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/HLOD/HLODModifier.h"
#include "WorldPartition/HLOD/HLODSourceActorsFromCell.h"
#include "WorldPartition/HLOD/HLODStats.h"
#include "WorldPartition/HLOD/HLODInstancedStaticMeshComponent.h"
#include "WorldPartition/HLOD/Builders/HLODBuilderCustomHLODActor.h"
#include "WorldPartition/HLOD/Builders/HLODBuilderInstancing.h"
#include "WorldPartition/HLOD/Builders/HLODBuilderMeshMerge.h"
#include "WorldPartition/HLOD/Builders/HLODBuilderMeshSimplify.h"
#include "WorldPartition/HLOD/Builders/HLODBuilderMeshApproximate.h"

#include "WorldPartition/WorldPartitionHLODsBuilder.h"

#include "AssetCompilingManager.h"
#include "BodySetupEnums.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/RuntimeVirtualTextureComponent.h"
#include "VT/RuntimeVirtualTextureVolume.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Rendering/NaniteResources.h"
#include "Serialization/ArchiveCrc32.h"
#include "ShaderCompiler.h"
#include "StaticMeshCompiler.h"
#include "StaticMeshResources.h"
#include "TextureCompiler.h"
#include "UObject/MetaData.h"
#include "UObject/GCObjectScopeGuard.h"
#include "ContentStreaming.h"
#include "Materials/Material.h"

TAutoConsoleVariable<bool> CVarHLODUseLegacy32BitNameHash(
	TEXT("wp.Editor.HLOD.UseLegacy32BitNameHash"),
	false,
	TEXT("Force the use of 32-bit hashing to use when generating HLOD actor names. Switching to 64-bit will result in new names/new actor files."),
	ECVF_ReadOnly);

struct FPerHLODLayerKey
{
	UHLODLayer* Layer;
	bool bCustomHLOD;

	FPerHLODLayerKey(UHLODLayer* InLayer, bool bInCustomHLOD)
		: Layer(InLayer)
		, bCustomHLOD(bInCustomHLOD)
	{}

	bool operator==(const FPerHLODLayerKey& Other) const
	{
		return Layer == Other.Layer && bCustomHLOD == Other.bCustomHLOD;
	}

	friend uint32 GetTypeHash(const FPerHLODLayerKey& Key)
	{
		return HashCombine(GetTypeHash(Key.Layer), GetTypeHash(Key.bCustomHLOD));
	}
};

struct FPerHLODLayerData
{
	TMap<FGuid, FWorldPartitionRuntimeCellObjectMapping> SourceActors;
	FBox Bounds = FBox(ForceInit);
};

void AddSourceActor(const FStreamingGenerationActorDescView& ActorDescView, const FName WorldPackageName, FPerHLODLayerData& PerHLODLayerData)
{
	const FName ActorPath = *ActorDescView.GetActorSoftPath().ToString();

	const UActorDescContainerInstance* ContainerInstance = ActorDescView.GetContainerInstance();
	check(ContainerInstance);

	const FActorContainerID& ContainerID = ContainerInstance->GetContainerID();
	const FTransform ContainerTransform = ContainerInstance->GetTransform();
	const FName ContainerPackage = ContainerInstance->GetContainerPackage();

	PerHLODLayerData.Bounds += ActorDescView.GetRuntimeBounds();

	FGuid ActorInstanceGuid = ContainerID.GetActorGuid(ActorDescView.GetGuid());	
	FWorldPartitionRuntimeCellObjectMapping& ActorMapping = PerHLODLayerData.SourceActors.FindOrAdd(ActorInstanceGuid);
	if (!ActorMapping.ActorInstanceGuid.IsValid())
	{
		ActorMapping = FWorldPartitionRuntimeCellObjectMapping(
			ActorDescView.GetActorPackage(),
			*ActorDescView.GetActorSoftPath().ToString(),
			ActorDescView.GetBaseClass(),
			ActorDescView.GetNativeClass(),
			ContainerID,
			ContainerTransform,
			ActorDescView.GetEditorOnlyParentTransform(),
			ContainerPackage,
			WorldPackageName,
			ActorInstanceGuid,
			false);

		// Add its runtime references, recursively
		const FStreamingGenerationActorDescViewMap& ActorDescViewMap = ActorDescView.GetActorDescViewMap();
		for (const FGuid& ReferenceGuid : ActorDescView.GetReferences())
		{
			const FStreamingGenerationActorDescView& RefActorDescView = ActorDescViewMap.FindByGuidChecked(ReferenceGuid);
			AddSourceActor(RefActorDescView, WorldPackageName, PerHLODLayerData);
		}

		// Add its editor references
		for (const FGuid& EditorReferenceGuid : ActorDescView.GetEditorReferences())
		{
			const FWorldPartitionActorDescInstance& ReferenceActorDesc = ContainerInstance->GetActorDescInstanceChecked(EditorReferenceGuid);
			FGuid EditorRefInstanceGuid = ContainerID.GetActorGuid(EditorReferenceGuid);
			FWorldPartitionRuntimeCellObjectMapping& EditorRefMapping = PerHLODLayerData.SourceActors.FindOrAdd(EditorRefInstanceGuid);
			if (!EditorRefMapping.ActorInstanceGuid.IsValid())
			{
				EditorRefMapping = FWorldPartitionRuntimeCellObjectMapping(
					ReferenceActorDesc.GetActorPackage(),
					*ReferenceActorDesc.GetActorSoftPath().ToString(),
					ReferenceActorDesc.GetBaseClass(),
					ReferenceActorDesc.GetNativeClass(),
					ContainerID,
					ContainerTransform,
					FTransform::Identity,
					ContainerPackage,
					WorldPackageName,
					EditorRefInstanceGuid,
					true
				);
			}
		}
	}
}

uint64 ComputeHLODActorUniqueHash(const UHLODLayer* HLODLayer, const FGuid CellGuid, bool bCustomHLOD)
{
	uint64 HLODActorHash = 0;
	if (!CVarHLODUseLegacy32BitNameHash.GetValueOnGameThread())
	{
		const FString HLODLayerName = HLODLayer->GetName();
		HLODActorHash = CityHash64WithSeed(reinterpret_cast<const char*>(HLODLayerName.GetCharArray().GetData()), HLODLayerName.GetCharArray().Num() * sizeof(FString::ElementType), HLODActorHash);
		HLODActorHash = CityHash64WithSeed((char*)&CellGuid, sizeof(FGuid), HLODActorHash);
		if (HLODLayer->GetHLODActorClass() != AWorldPartitionHLOD::StaticClass())
		{
			const FString HLODActorClassPathName = HLODLayer->GetHLODActorClass()->GetPathName();
			HLODActorHash = CityHash64WithSeed(reinterpret_cast<const char*>(HLODActorClassPathName.GetCharArray().GetData()), HLODActorClassPathName.GetCharArray().Num() * sizeof(FString::ElementType), HLODActorHash);
		}
		if (bCustomHLOD)
		{
			static const FString CustomHLODText(TEXT("CustomHLOD"));
			HLODActorHash = CityHash64WithSeed(reinterpret_cast<const char*>(CustomHLODText.GetCharArray().GetData()), CustomHLODText.GetCharArray().Num() * sizeof(FString::ElementType), HLODActorHash);
		}
	}
	else
	{
		const uint32 HLODLayerNameHash = FCrc::StrCrc32(*HLODLayer->GetName());
		const uint32 CellGuidHash = GetTypeHash(CellGuid);
		uint32 HLODActorHash32 = HashCombineFast(HLODLayerNameHash, CellGuidHash);
		if (HLODLayer->GetHLODActorClass() != AWorldPartitionHLOD::StaticClass())
		{
			const uint32 HLODActorClassHash = FCrc::StrCrc32(*HLODLayer->GetHLODActorClass()->GetPathName());
			HLODActorHash32 = HashCombineFast(HLODActorHash32, HLODActorClassHash);
		}
		if (bCustomHLOD)
		{
			static const FString CustomHLODText(TEXT("CustomHLOD"));
			const uint32 CustomHLODTextHash = GetTypeHash(CustomHLODText);
			HLODActorHash32 = HashCombineFast(HLODActorHash32, CustomHLODTextHash);
		}
		HLODActorHash = HLODActorHash32;
	}
	return HLODActorHash;			
}

TArray<AWorldPartitionHLOD*> FWorldPartitionHLODUtilities::CreateHLODActors(FHLODCreationContext& InCreationContext, const FHLODCreationParams& InCreationParams, const TArray<IStreamingGenerationContext::FActorInstance>& InActors)
{
	TMap<FPerHLODLayerKey, FPerHLODLayerData> PerHLODLayerDataMap;

	FName WorldPackageName = InCreationParams.WorldPartition->GetWorld()->GetPackage()->GetFName();

	for (const IStreamingGenerationContext::FActorInstance& ActorInstance : InActors)
	{
		const FStreamingGenerationActorDescView& ActorDescView = ActorInstance.GetActorDescView();

		if (ActorDescView.GetActorIsHLODRelevant())
		{
			if (!ActorInstance.ActorSetInstance->bIsSpatiallyLoaded)
			{
				UE_LOGF(LogHLODBuilder, Warning, "Tried to included non-spatially loaded actor %ls into HLOD", *ActorDescView.GetActorName().ToString());
				continue;
			}

			if (UHLODLayer* HLODLayer = Cast<UHLODLayer>(ActorDescView.GetHLODLayer().TryLoad()))
			{
				if (HLODLayer->GetLayerType() == EHLODLayerType::CustomHLODActor)
				{
					// Actors in the CustomHLODActor layer will be directly included in the HLOD representation, so we can use it as a source actor in the parent layer
					// Or skip it if there's no parent layer
					if (HLODLayer->GetParentLayer())
					{
						HLODLayer = HLODLayer->GetParentLayer();
					}
					else
					{
						continue;
					}
				}

				const bool bIsCustomHLODActor = ActorDescView.GetActorNativeClass()->IsChildOf<AWorldPartitionCustomHLOD>();
				FPerHLODLayerData& PerHLODLayerData = PerHLODLayerDataMap.FindOrAdd(FPerHLODLayerKey(HLODLayer, bIsCustomHLODActor));
				AddSourceActor(ActorDescView, WorldPackageName, PerHLODLayerData);
			}
		}
	}

	TArray<AWorldPartitionHLOD*> HLODActors;
	for (auto& Pair : PerHLODLayerDataMap)
	{
		const UHLODLayer* HLODLayer = Pair.Key.Layer;
		const bool bCustomHLOD = Pair.Key.bCustomHLOD;
		const FBox& Bounds = Pair.Value.Bounds;
		TMap<FGuid, FWorldPartitionRuntimeCellObjectMapping>& SourceActorsMap = Pair.Value.SourceActors;
		check(!SourceActorsMap.IsEmpty());

		uint64 HLODActorHash = ComputeHLODActorUniqueHash(HLODLayer, InCreationParams.CellGuid, bCustomHLOD);
		FName HLODActorName = *FString::Printf(TEXT("%s_%016" UINT64_x_FMT), *HLODLayer->GetName(), HLODActorHash);
		FName HLODResourcesPackagePath = NAME_None;
		if (!InCreationParams.HLODResourcesFolder.IsNone())
		{
			HLODResourcesPackagePath = (FName(InCreationParams.HLODResourcesFolder.ToString() + HLODActorName.ToString()));
		}

		AWorldPartitionHLOD* HLODActor = nullptr;
		FWorldPartitionHandle HLODActorHandle;
		if (InCreationContext.HLODActorDescs.RemoveAndCopyValue(HLODActorName, HLODActorHandle))
		{
			InCreationContext.ActorReferences.Add(HLODActorHandle.ToReference());
			HLODActor = CastChecked<AWorldPartitionHLOD>(HLODActorHandle.GetActor());
		}

		// If there was no ActorDesc found, check if we can find this actor in UnsavedHLODActors
		TWeakObjectPtr<AWorldPartitionHLOD> HLODActorPtr;
		if (HLODActor == nullptr && InCreationContext.UnsavedHLODActors.RemoveAndCopyValue(HLODActorName, HLODActorPtr))
		{
			HLODActor = HLODActorPtr.Get();
		}

		FHLODCreationFilterContext FilterContext;
		FilterContext.Bounds = Bounds;
		const bool bPassesAllFilters = UE::HLOD::CreationFilter::PassesFilters(InCreationParams.Filters, FilterContext);
		if (!bPassesAllFilters)
		{
			if (HLODActor)
			{
				// If HLOD actor doesn't pass any of the filters, don't create it or update it. But if it exists, add it to HLODActors, so that it can be used when processing next layer
				HLODActors.Add(HLODActor);
			}
			continue;
		}

		bool bNewActor = HLODActor == nullptr;
		if (bNewActor)
		{
			FContentBundleActivationScope Activationscope(InCreationParams.ContentBundleGuid);
			FScopedOverrideSpawningLevelMountPointObject EDLScope(InCreationParams.GetExternalDataLayerAsset());

			FActorSpawnParameters SpawnParams;
			SpawnParams.Name = HLODActorName;
			SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_Fatal;
			HLODActor = InCreationParams.TargetWorld->SpawnActor<AWorldPartitionHLOD>(HLODLayer->GetHLODActorClass(), SpawnParams);

			check(HLODActor->GetContentBundleGuid() == InCreationParams.ContentBundleGuid);
			check(HLODActor->GetExternalDataLayerAsset() == InCreationParams.GetExternalDataLayerAsset());

			HLODActor->SetSourceCellGuid(InCreationParams.CellGuid);

			// Make sure the generated HLOD actor has the same data layers as the source actors
			for (const UDataLayerInstance* DataLayerInstance : InCreationParams.DataLayerInstances)
			{
				if (!DataLayerInstance->IsA<UExternalDataLayerInstance>())
				{
					HLODActor->AddDataLayer(DataLayerInstance);
				}
			}

			HLODActor->SetHLODResourcesPackagePath(HLODResourcesPackagePath);
		}
		else
		{
			check(HLODActor->GetSourceCellGuid() == InCreationParams.CellGuid);
			check(HLODActor->GetClass() == HLODLayer->GetHLODActorClass());
			check(HLODActor->GetExternalDataLayerAsset() == InCreationParams.GetExternalDataLayerAsset());
		}

		const TCHAR* DirtyReason = nullptr;

		// Source actors object
		UWorldPartitionHLODSourceActorsFromCell* HLODSourceActors = Cast<UWorldPartitionHLODSourceActorsFromCell>(HLODActor->GetSourceActors());
		if (!HLODSourceActors)
		{
			HLODSourceActors = NewObject<UWorldPartitionHLODSourceActorsFromCell>(HLODActor);
			HLODSourceActors->SetHLODLayer(HLODLayer);
			HLODActor->SetSourceActors(HLODSourceActors);
			DirtyReason = TEXT("SourceActors");
		}
		check(HLODSourceActors->GetHLODLayer() == HLODLayer);

		// Assign source actors
		{
			TArray<FWorldPartitionRuntimeCellObjectMapping> SourceActorsArray;
			SourceActorsMap.KeySort(TLess<FGuid>());
			SourceActorsMap.GenerateValueArray(SourceActorsArray);

			if (InCreationParams.bIsStandalone)
			{
				for (FWorldPartitionRuntimeCellObjectMapping& SourceActor : SourceActorsArray)
				{
					if (SourceActor.ContainerID.IsMainContainer())
					{
						// Set ContainerID for Standalone HLOD Source Actors to allow them to be loaded in the context of Standalone HLOD Worlds for HLOD build
						SourceActor.ContainerID = FActorContainerID(FActorContainerID::GetMainContainerID(), FGuid::NewDeterministicGuid(SourceActor.ContainerPackage.ToString()));
					}
				}
			}

			bool bSourceActorsChanged = HLODSourceActors->GetActors().Num() != SourceActorsArray.Num();
			if (!bSourceActorsChanged)
			{
				const uint32 NewHash = UWorldPartitionHLODSourceActorsFromCell::GetSourceActorsHash(SourceActorsArray);
				const uint32 PreviousHash = UWorldPartitionHLODSourceActorsFromCell::GetSourceActorsHash(HLODSourceActors->GetActors());
				bSourceActorsChanged = NewHash != PreviousHash;
			}

			if (bSourceActorsChanged)
			{
				HLODSourceActors->SetActors(MoveTemp(SourceActorsArray));
				DirtyReason = TEXT("SourceActors");
			}
		}

		// Runtime grid
		const FName RuntimeGrid = InCreationParams.GetRuntimeGrid(HLODLayer);
		if (HLODActor->GetRuntimeGrid() != RuntimeGrid)
		{
			HLODActor->SetRuntimeGrid(RuntimeGrid);
			DirtyReason = TEXT("RuntimeGrid");
		}

		// Spatially loaded
		const bool bIsSpatiallyLoadedHLOD = !RuntimeGrid.IsNone();
		if (HLODActor->GetIsSpatiallyLoaded() != bIsSpatiallyLoadedHLOD)
		{
			HLODActor->SetIsSpatiallyLoaded(bIsSpatiallyLoadedHLOD);
			DirtyReason = TEXT("SpatiallyLoaded");
		}

		// HLOD level
		if (HLODActor->GetLODLevel() != InCreationParams.HLODLevel)
		{
			HLODActor->SetLODLevel(InCreationParams.HLODLevel);
			DirtyReason = TEXT("HLODLevel");
		}

		// Require warmup
		if (HLODActor->DoesRequireWarmup() != HLODLayer->DoesRequireWarmup())
		{
			HLODActor->SetRequireWarmup(HLODLayer->DoesRequireWarmup());
			DirtyReason = TEXT("DoesRequireWarmup");
		}

		// Parent HLOD layer
		UHLODLayer* ParentHLODLayer = bIsSpatiallyLoadedHLOD ? HLODLayer->GetParentLayer() : nullptr;
		if (HLODActor->GetHLODLayer() != ParentHLODLayer)
		{
			HLODActor->SetHLODLayer(ParentHLODLayer);
			DirtyReason = TEXT("ParentHLODLayer");
		}

		// Actor label
		const FString ActorLabel = FString::Printf(TEXT("%s/%s%s"), *HLODLayer->GetName(), *InCreationParams.CellName, bCustomHLOD ? TEXT("_CustomHLOD") : TEXT(""));
		if (HLODActor->GetActorLabel() != ActorLabel)
		{
			HLODActor->SetActorLabel(ActorLabel);
			DirtyReason = TEXT("ActorLabel");
		}

		// Folder name
		const FName FolderPath(FString::Printf(TEXT("HLOD/%s"), *HLODLayer->GetName()));
		if (HLODActor->GetFolderPath() != FolderPath)
		{
			HLODActor->SetFolderPath(FolderPath);
			DirtyReason = TEXT("FolderPath");
		}

		// HLOD bounds
		if (!HLODActor->GetHLODBounds().Equals(Bounds))
		{
			HLODActor->SetHLODBounds(Bounds);
			DirtyReason = TEXT("HLODBounds");
		}

		// Minimum visible distance
		if (!FMath::IsNearlyEqual(HLODActor->GetMinVisibleDistance(), InCreationParams.MinVisibleDistance))
		{
			HLODActor->SetMinVisibleDistance(InCreationParams.MinVisibleDistance);
			DirtyReason = TEXT("MinVisibleDistance");
		}

		if (HLODActor->IsStandalone() != InCreationParams.bIsStandalone)
		{
			HLODActor->SetIsStandalone(InCreationParams.bIsStandalone);
			DirtyReason = TEXT("Standalone");
		}

		if (HLODActor->GetHLODResourcesPackagePath() != HLODResourcesPackagePath)
		{
			UE_LOGF(LogHLODBuilder, Error, "Changing or removing HLOD's external resources package path is not supported. Please delete HLODs and try again.");
		}

		// If any change was performed, mark HLOD package as dirty
		if (DirtyReason)
		{
			HLODActor->MarkPackageDirty();
			HLODActor->UpdateHLODBuildReportHeader();
			UE_CLOGF(!bNewActor, LogHLODBuilder, Log, "Marking existing HLOD actor \"%ls\" dirty, reason \"%ls\".", *HLODActor->GetActorLabel(), DirtyReason);
		}

		HLODActors.Add(HLODActor);
	}

	return HLODActors;
}

TSubclassOf<UHLODBuilder> FWorldPartitionHLODUtilities::GetHLODBuilderClass(const UHLODLayer* InHLODLayer)
{
	EHLODLayerType HLODLayerType = InHLODLayer->GetLayerType();
	switch (HLODLayerType)
	{
	case EHLODLayerType::Instancing:
		return UHLODBuilderInstancing::StaticClass();

	case EHLODLayerType::MeshMerge:
		return UHLODBuilderMeshMerge::StaticClass();

	case EHLODLayerType::MeshSimplify:
		return UHLODBuilderMeshSimplify::StaticClass();

	case EHLODLayerType::MeshApproximate:
		return UHLODBuilderMeshApproximate::StaticClass();

	case EHLODLayerType::Custom:
		return InHLODLayer->GetHLODBuilderClass();

	case EHLODLayerType::CustomHLODActor:
		return UHLODBuilderCustomHLODActor::StaticClass();

	default:
		checkf(false, TEXT("Unsupported type"));
		return nullptr;
	}
}

UHLODBuilderSettings* FWorldPartitionHLODUtilities::CreateHLODBuilderSettings(UHLODLayer* InHLODLayer)
{
	// Retrieve the HLOD builder class
	TSubclassOf<UHLODBuilder> HLODBuilderClass = GetHLODBuilderClass(InHLODLayer);
	if (!HLODBuilderClass)
	{
		return NewObject<UHLODBuilderSettings>(InHLODLayer, UHLODBuilderSettings::StaticClass());
	}

	// Retrieve the HLOD builder settings class
	TSubclassOf<UHLODBuilderSettings> HLODBuilderSettingsClass = HLODBuilderClass->GetDefaultObject<UHLODBuilder>()->GetSettingsClass();
	if (!ensure(HLODBuilderSettingsClass))
	{
		return NewObject<UHLODBuilderSettings>(InHLODLayer, UHLODBuilderSettings::StaticClass());
	}

	UHLODBuilderSettings* HLODBuilderSettings = NewObject<UHLODBuilderSettings>(InHLODLayer, HLODBuilderSettingsClass);

	// Deprecated properties handling
	if (InHLODLayer->GetHLODBuilderSettings() == nullptr)
	{
		EHLODLayerType HLODLayerType = InHLODLayer->GetLayerType();
		switch (HLODLayerType)
		{
		case EHLODLayerType::MeshMerge:
			CastChecked<UHLODBuilderMeshMergeSettings>(HLODBuilderSettings)->MeshMergeSettings = InHLODLayer->MeshMergeSettings_DEPRECATED;
			CastChecked<UHLODBuilderMeshMergeSettings>(HLODBuilderSettings)->HLODMaterial = InHLODLayer->HLODMaterial_DEPRECATED.LoadSynchronous();
			break;

		case EHLODLayerType::MeshSimplify:
			CastChecked<UHLODBuilderMeshSimplifySettings>(HLODBuilderSettings)->MeshSimplifySettings = InHLODLayer->MeshSimplifySettings_DEPRECATED;
			CastChecked<UHLODBuilderMeshSimplifySettings>(HLODBuilderSettings)->HLODMaterial = InHLODLayer->HLODMaterial_DEPRECATED.LoadSynchronous();
			break;

		case EHLODLayerType::MeshApproximate:
			CastChecked<UHLODBuilderMeshApproximateSettings>(HLODBuilderSettings)->MeshApproximationSettings = InHLODLayer->MeshApproximationSettings_DEPRECATED;
			CastChecked<UHLODBuilderMeshApproximateSettings>(HLODBuilderSettings)->HLODMaterial = InHLODLayer->HLODMaterial_DEPRECATED.LoadSynchronous();
			break;
		};
	}

	return HLODBuilderSettings;
}

void GatherInputStats(AWorldPartitionHLOD* InHLODActor, const TArray<UActorComponent*> InHLODRelevantComponents)
{
	int64 NumActors = 0;
	int64 NumTriangles = 0;
	int64 NumVertices = 0;

	TSet<AActor*> HLODRelevantActors;

	for (UActorComponent* HLODRelevantComponent : InHLODRelevantComponents)
	{
		bool bAlreadyInSet = false;
		AActor* SubActor = HLODRelevantActors.FindOrAdd(HLODRelevantComponent->GetOwner(), &bAlreadyInSet);

		if (!bAlreadyInSet)
		{
			if (AWorldPartitionHLOD* SubHLODActor = Cast<AWorldPartitionHLOD>(SubActor))
			{
				NumActors += SubHLODActor->GetStat(FWorldPartitionHLODStats::InputActorCount);
				NumTriangles += SubHLODActor->GetStat(FWorldPartitionHLODStats::InputTriangleCount);
				NumVertices += SubHLODActor->GetStat(FWorldPartitionHLODStats::InputVertexCount);
			}
			else
			{
				NumActors++;
			}
		}

		if (!SubActor->IsA<AWorldPartitionHLOD>())
		{
			if (UStaticMeshComponent* SMComponent = Cast<UStaticMeshComponent>(HLODRelevantComponent))
			{
				const UStaticMesh* StaticMesh = SMComponent->GetStaticMesh();
				const FStaticMeshRenderData* RenderData = StaticMesh ? StaticMesh->GetRenderData() : nullptr;
				const bool bHasRenderData = RenderData && !RenderData->LODResources.IsEmpty();

				if (bHasRenderData)
				{
					const UInstancedStaticMeshComponent* ISMComponent = Cast<UInstancedStaticMeshComponent>(SMComponent);
					const int64 LOD0TriCount = RenderData->LODResources[0].GetNumTriangles();
					const int64 LOD0VtxCount = RenderData->LODResources[0].GetNumVertices();
					const int64 NumInstances = ISMComponent ? ISMComponent->GetInstanceCount() : 1;
					
					NumTriangles += LOD0TriCount * NumInstances;
					NumVertices += LOD0VtxCount * NumInstances;
				}
			}
		}
	}

	InHLODActor->SetStat(FWorldPartitionHLODStats::InputActorCount, NumActors);
	InHLODActor->SetStat(FWorldPartitionHLODStats::InputTriangleCount, NumTriangles);
	InHLODActor->SetStat(FWorldPartitionHLODStats::InputVertexCount, NumVertices);	
}

void GatherOutputStats(AWorldPartitionHLOD* InHLODActor)
{
	const UPackage* HLODResourcesPackage = InHLODActor->GetHLODResourcesPackage();
	check(HLODResourcesPackage);

	// Gather relevant assets and process them outside of this ForEach as it's possible that async compilation completion
	// triggers insertion of new objects, which would break the iteration over UObjects
	TArray<UStaticMesh*> StaticMeshes;
	TArray<UTexture*> Textures;
	TArray<UMaterialInstance*> MaterialInstances;

	ForEachObjectWithPackage(HLODResourcesPackage, [&](UObject* Object)
	{
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Object))
		{
			StaticMeshes.Add(StaticMesh);
		}
		else if (UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(Object))
		{
			MaterialInstances.Add(MaterialInstance);
		}
		else if (UTexture* Texture = Cast<UTexture>(Object))
		{
			Textures.Add(Texture);
		}
		return true;
	}, EGetObjectsFlags::None);

	// Process static meshes
	int64 MeshResourceSize = 0;
	{
		int64 InstanceCount = 0;
		int64 NaniteTriangleCount = 0;
		int64 NaniteVertexCount = 0;
		int64 TriangleCount = 0;
		int64 VertexCount = 0;
		int64 UVChannelCount = 0;

		InHLODActor->ForEachComponent<UInstancedStaticMeshComponent>(false, [&](const UInstancedStaticMeshComponent* ISMC)
		{
			InstanceCount += ISMC->GetInstanceCount();
		});

		FStaticMeshCompilingManager::Get().FinishCompilation(StaticMeshes);

		for (UStaticMesh* StaticMesh : StaticMeshes)
		{
			MeshResourceSize += StaticMesh->GetResourceSizeBytes(EResourceSizeMode::Exclusive);

			TriangleCount += StaticMesh->GetNumTriangles(0);
			VertexCount += StaticMesh->GetNumVertices(0);
			UVChannelCount = FMath::Max(UVChannelCount, StaticMesh->GetNumTexCoords(0));

			NaniteTriangleCount += StaticMesh->GetNumNaniteTriangles();
			NaniteVertexCount += StaticMesh->GetNumNaniteVertices();
		}

		// Mesh stats
		InHLODActor->SetStat(FWorldPartitionHLODStats::MeshInstanceCount, InstanceCount);
		InHLODActor->SetStat(FWorldPartitionHLODStats::MeshNaniteTriangleCount, NaniteTriangleCount);
		InHLODActor->SetStat(FWorldPartitionHLODStats::MeshNaniteVertexCount, NaniteVertexCount);
		InHLODActor->SetStat(FWorldPartitionHLODStats::MeshTriangleCount, TriangleCount);
		InHLODActor->SetStat(FWorldPartitionHLODStats::MeshVertexCount, VertexCount);
		InHLODActor->SetStat(FWorldPartitionHLODStats::MeshUVChannelCount, UVChannelCount);
	}

	// Process materials
	int64 TexturesResourceSize = 0;
	{
		int64 BaseColorTextureSize = 0;
		int64 NormalTextureSize = 0;
		int64 EmissiveTextureSize = 0;
		int64 MetallicTextureSize = 0;
		int64 RoughnessTextureSize = 0;
		int64 SpecularTextureSize = 0;

		FTextureCompilingManager::Get().FinishCompilation(Textures);

		for (UMaterialInstance* MaterialInstance : MaterialInstances)
		{
			// Retrieve the texture size for a texture that can have different names
			auto GetTextureSize = [&](const TArray<FName>& TextureParamNames) -> int64
			{
				for (FName TextureParamName : TextureParamNames)
				{
					UTexture* Texture = nullptr;
					MaterialInstance->GetTextureParameterValue(TextureParamName, Texture, true);

					if (Texture && Texture->GetPackage() == HLODResourcesPackage)
					{
						TexturesResourceSize += Texture->GetResourceSizeBytes(EResourceSizeMode::Exclusive);
						return FMath::RoundToInt64(Texture->GetSurfaceWidth());
					}
				}

				return 0;
			};

			int64 LocalBaseColorTextureSize = GetTextureSize({ "BaseColorTexture", "DiffuseTexture" });
			int64 LocalNormalTextureSize = GetTextureSize({ "NormalTexture" });
			int64 LocalEmissiveTextureSize = GetTextureSize({ "EmissiveTexture", "EmissiveColorTexture" });
			int64 LocalMetallicTextureSize = GetTextureSize({ "MetallicTexture" });
			int64 LocalRoughnessTextureSize = GetTextureSize({ "RoughnessTexture" });
			int64 LocalSpecularTextureSize = GetTextureSize({ "SpecularTexture" });

			int64 MRSTextureSize = GetTextureSize({ "PackedTexture" });
			if (MRSTextureSize != 0)
			{
				LocalMetallicTextureSize = LocalRoughnessTextureSize = LocalSpecularTextureSize = MRSTextureSize;
			}

			BaseColorTextureSize = FMath::Max(BaseColorTextureSize, LocalBaseColorTextureSize);
			NormalTextureSize = FMath::Max(NormalTextureSize, LocalNormalTextureSize);
			EmissiveTextureSize = FMath::Max(EmissiveTextureSize, LocalEmissiveTextureSize);
			MetallicTextureSize = FMath::Max(MetallicTextureSize, LocalMetallicTextureSize);
			RoughnessTextureSize = FMath::Max(RoughnessTextureSize, LocalRoughnessTextureSize);
			SpecularTextureSize = FMath::Max(SpecularTextureSize, LocalSpecularTextureSize);
		}

		// Material stats
		InHLODActor->SetStat(FWorldPartitionHLODStats::MaterialBaseColorTextureSize, BaseColorTextureSize);
		InHLODActor->SetStat(FWorldPartitionHLODStats::MaterialNormalTextureSize, NormalTextureSize);
		InHLODActor->SetStat(FWorldPartitionHLODStats::MaterialEmissiveTextureSize, EmissiveTextureSize);
		InHLODActor->SetStat(FWorldPartitionHLODStats::MaterialMetallicTextureSize, MetallicTextureSize);
		InHLODActor->SetStat(FWorldPartitionHLODStats::MaterialRoughnessTextureSize, RoughnessTextureSize);
		InHLODActor->SetStat(FWorldPartitionHLODStats::MaterialSpecularTextureSize, SpecularTextureSize);
	}

	// Memory stats
	InHLODActor->SetStat(FWorldPartitionHLODStats::MemoryMeshResourceSizeBytes, MeshResourceSize);
	InHLODActor->SetStat(FWorldPartitionHLODStats::MemoryTexturesResourceSizeBytes, TexturesResourceSize);
}

// Iterate over the source actors and retrieve HLOD relevant components using GetHLODRelevantComponents()
static TArray<UActorComponent*> GatherHLODRelevantComponents(UWorld* InWorld, const UWorldPartitionHLODSourceActors& InSourceActors)
{
	TSet<UActorComponent*> HLODRelevantComponents;

	for (TActorIterator<AActor> It(InWorld); It; ++It)
	{
		if (!InSourceActors.IsHLODRelevant(*It))
		{
			continue;
		}

		// Extract components to be used as input for the HLOD generation process
		for (UActorComponent* HLODRelevantComponentForActor : It->GetHLODRelevantComponents())
		{
			// Components can return proxy components to be used in their place while building HLODs
			TArray<UActorComponent*> HLODProxyComponents = HLODRelevantComponentForActor->GetHLODProxyComponents();
			if (!HLODProxyComponents.IsEmpty())
			{
				// Use proxy components
				HLODRelevantComponents.Append(HLODProxyComponents);
			}
			else
			{
				// Use the original component
				HLODRelevantComponents.Add(HLODRelevantComponentForActor);
			}
		}
	}

	return HLODRelevantComponents.Array();
}

bool LoadSourceActors(const AWorldPartitionHLOD* InHLODActor, UWorld* TargetWorld, bool& bOutIsDirty)
{
	// If we're loading actors for an HLOD > 0
	// Ensure that async writes for source actors (which are HLODs N-1) are completed.
	if (InHLODActor->GetLODLevel() > 0)
	{
		UPackage::WaitForAsyncFileWrites();
	}
	
	bool bLoadSuccess = InHLODActor->GetSourceActors()->LoadSourceActors(bOutIsDirty, TargetWorld);
	UE_CLOGF(bOutIsDirty, LogHLODBuilder, Warning, "HLOD actor \"%ls\" needs to be rebuilt as it didn't succeed in loading all actors.", *InHLODActor->GetActorLabel());

	if (bLoadSuccess)
	{
		// Finish assets compilation
		FAssetCompilingManager::Get().FinishAllCompilation();

		// Ensure all deferred construction scripts are executed
		FAssetCompilingManager::Get().ProcessAsyncTasks();

		// Ensure streaming requests are completed
		// The outer loop calls StreamAllResources to discover pending assets and kick off I/O.
		// The inner loop uses BlockTillAllRequestsFinished to drain in-flight requests without
		// calling UpdateResourceStreaming, which would restart them and prevent forward progress.
		// When the inner loop finishes, the outer loop re-checks for assets the tick may have registered.
		const float STREAMING_WAIT_DT = 0.1f;
		while (IStreamingManager::Get().StreamAllResources(STREAMING_WAIT_DT) > 0)
		{
			do
			{
				// Application tick.
				FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
				FTSTicker::GetCoreTicker().Tick(static_cast<float>(FApp::GetDeltaTime()));
			}
			while (IStreamingManager::Get().BlockTillAllRequestsFinished(STREAMING_WAIT_DT, true) > 0);
		}
	
		// Make sure all referenced materials are compiled
		for (TActorIterator<AActor> It(TargetWorld); It; ++It)
		{
			It->ForEachComponent<UPrimitiveComponent>(false, [](UPrimitiveComponent* PrimComp)
			{
				TArray<UMaterialInterface*> UsedMaterials;
				PrimComp->GetUsedMaterials(UsedMaterials);
				for (UMaterialInterface* Mat : UsedMaterials)
				{
					// Walk the full parent chain — the incomplete shader map may be on a parent MIC, not the leaf
					UMaterialInterface* Current = Mat;
					while (Current)
					{
						Current->EnsureIsComplete();
						UMaterialInstance* MI = Cast<UMaterialInstance>(Current);
						Current = MI ? MI->Parent.Get() : nullptr;
					}
				}
			});
		}

		GShaderCompilingManager->FinishAllCompilation();
	}

	return bLoadSuccess;
}

void UnloadSourceActors(ULevelStreaming* InLevelStreaming)
{
	UWorld* World = InLevelStreaming->GetWorld();

	InLevelStreaming->SetShouldBeVisibleInEditor(false);
	InLevelStreaming->SetIsRequestingUnloadAndRemoval(true);

	if (ULevel* Level = InLevelStreaming->GetLoadedLevel())
	{
		World->RemoveLevel(Level);
		World->FlushLevelStreaming();

		// Destroy the package world and remove it from root
		UPackage* Package = Level->GetPackage();
		UWorld* PackageWorld = UWorld::FindWorldInPackage(Package);
		if (ensure(PackageWorld))
		{
			PackageWorld->DestroyWorld(false);
		}
	}
}

bool FWorldPartitionHLODUtilities::BuildHLOD(const FHLODBuildParams& InBuildParams)
{
	FAutoScopedDurationTimer TotalTimeScope;

	// Keep track of timings related to this build
	int64 LoadTimeMS = 0;
	int64 BuildTimeMS = 0;
	int64 TotalTimeMS = 0;

	AWorldPartitionHLOD* HLODActor = InBuildParams.HLODActor;
	
	// Load actors relevant to HLODs
	bool bIsDirty = false;

	const bool bInformEngineOfWorld = false;
	const bool bAddToRoot = true;
	UPackage* BuildHLODPackage = NewObject<UPackage>(nullptr, *MakeUniqueObjectName(nullptr, UPackage::StaticClass(), TEXT("/Temp/BuildHLODPackage")).ToString(), RF_Transient);
	UWorld* BuildHLODWorld = UWorld::CreateWorld(EWorldType::Editor, bInformEngineOfWorld, TEXT("BuildHLODWorld"), BuildHLODPackage, bAddToRoot);
	check(BuildHLODWorld);

	UWorld* SourceWorld = HLODActor->GetWorld();

	// Mirror the source world's RVT volumes into BuildHLODWorld so that materials sampling RVT
	// (e.g. landscape) render correctly during HLOD capture.
	{
		TArray<ARuntimeVirtualTextureVolume*> SourceRVTVolumes;
		for (TActorIterator<ARuntimeVirtualTextureVolume> It(SourceWorld); It; ++It)
		{
			SourceRVTVolumes.Add(*It);
		}

		if (SourceRVTVolumes.Num())
		{
			UActorContainer* ActorContainer = NewObject<UActorContainer>(SourceWorld->PersistentLevel);

			for (ARuntimeVirtualTextureVolume* SourceVolume : SourceRVTVolumes)
			{
				SourceVolume->UObject::Rename(nullptr, ActorContainer, REN_DoNotDirty);
				ActorContainer->Actors.Add(SourceVolume->GetFName(), SourceVolume);
			}

			FObjectDuplicationParameters Parameters(ActorContainer, BuildHLODWorld->PersistentLevel);
			Parameters.DestClass = ActorContainer->GetClass();
			Parameters.FlagMask = RF_AllFlags & ~(RF_MarkAsRootSet | RF_MarkAsNative | RF_HasExternalPackage);
			Parameters.InternalFlagMask = EInternalObjectFlags_AllFlags;
			Parameters.DuplicationSeed.Add(SourceWorld->PersistentLevel, BuildHLODWorld->PersistentLevel);

			UActorContainer* ActorContainerDup = (UActorContainer*)StaticDuplicateObjectEx(Parameters);
			check(ActorContainerDup);

			for (TPair<FName, AActor*> Pair : ActorContainerDup->Actors)
			{
				Pair.Value->Rename(nullptr, BuildHLODWorld->PersistentLevel, REN_DoNotDirty);
			}

			for (ARuntimeVirtualTextureVolume* SourceVolume : SourceRVTVolumes)
			{
				SourceVolume->UObject::Rename(nullptr, SourceWorld->PersistentLevel, REN_DoNotDirty);
			}

			ActorContainer->MarkAsGarbage();
			ActorContainerDup->MarkAsGarbage();
		}
	}

	ON_SCOPE_EXIT
	{
		BuildHLODWorld->DestroyWorld(bInformEngineOfWorld);

		// Once the build world (and its RVT proxies) are gone the shared URuntimeVirtualTexture
		// asset is left with a null producer - source-world RVT sampling would break until
		// something forced re-registration (map reload, material recompile). Mark the source
		// components dirty so they re-Initialize the asset and reclaim the producer slot.
		for (TActorIterator<ARuntimeVirtualTextureVolume> It(SourceWorld); It; ++It)
		{
			if (URuntimeVirtualTextureComponent* RVTComponent = It->VirtualTextureComponent)
			{
				RVTComponent->MarkRenderStateDirty();
			}
		}
	};

	{
		FAutoScopedDurationTimer LoadTimeScope;

		bool bLoadSucceeded = LoadSourceActors(HLODActor, BuildHLODWorld, bIsDirty);
		if (!ensure(bLoadSucceeded))
		{
			UE_LOGF(LogHLODBuilder, Error, "FWorldPartitionHLODUtilities::BuildHLOD() - Failed to load source actors");
			return 0;
		}

		LoadTimeMS = FMath::RoundToInt(LoadTimeScope.GetTime() * 1000);
	}

	const bool bForceBuild = bIsDirty || InBuildParams.bForceBuild;

	const UWorldPartitionHLODSourceActors* SourceActors = HLODActor->GetSourceActors();
	check(SourceActors);
	TArray<UActorComponent*> HLODRelevantComponents = GatherHLODRelevantComponents(BuildHLODWorld, *SourceActors);

	FHLODRebuildPolicyDataSet OldRebuildPolicyDataSet = bForceBuild ? FHLODRebuildPolicyDataSet() : HLODActor->GetHLODRebuildPolicyDataSet();
	FHLODRebuildPolicyDataSet NewRebuildPolicyDataSet = UHLODRebuildPolicy::ComputeDataForRebuildPolicies(HLODActor, HLODRelevantComponents, /*bInForComparison=*/true);
	EHLODRebuildPolicyDecision RebuildDecision;

	if (HLODBuildEvaluatorDelegate.IsBound())
	{
		RebuildDecision = HLODBuildEvaluatorDelegate.Execute(HLODActor, OldRebuildPolicyDataSet, NewRebuildPolicyDataSet);
	}
	else
	{
		RebuildDecision = UHLODRebuildPolicy::Evaluate(HLODActor, OldRebuildPolicyDataSet, NewRebuildPolicyDataSet);
	}

	if (RebuildDecision != EHLODRebuildPolicyDecision::ApproveRebuild)
	{
		return false;
	}

	// At this point, we know the HLOD would need to be rebuilt
	if (InBuildParams.bTestOnly)
	{
		return true;
	}

	// Clear stats as we're about to refresh them
	HLODActor->ResetStats();

	// Rename previous assets found in the HLOD actor package or HLOD resources package.
	// Move the previous asset(s) to the transient package, to avoid any object reuse during the build
	{
		TArray<UObject*> ObjectsToRename;
		UPackage* HLODResourcesPackage = HLODActor->GetHLODResourcesPackage();

		// Only perform package-wide cleanup when the package is dedicated to the HLOD (OFPA or external resources).
		// When the HLOD actor lives in a world package (e.g. FortWorldSettings Level HLOD), iterating
		// the package would incorrectly rename world objects (UMetaData, etc.) to the transient package.
		if (HLODResourcesPackage && HLODResourcesPackage != HLODActor->GetWorld()->GetPackage())
		{
			ForEachObjectWithOuter(HLODResourcesPackage, [&ObjectsToRename](UObject* Obj)
			{
				if (!Obj->IsA<AActor>() && !Obj->IsA<UWorld>())
				{
					ObjectsToRename.Add(Obj);
				}
			}, EGetObjectsFlags::None);

			for (UObject* Obj : ObjectsToRename)
			{
				// Make sure the old object is not used by anything
				Obj->ClearFlags(RF_Standalone | RF_Public);
				const FName OldRenamed = MakeUniqueObjectName(GetTransientPackage(), Obj->GetClass(), *FString::Printf(TEXT("OLD_%s"), *Obj->GetName()));
				Obj->Rename(*OldRenamed.ToString(), GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			}
		}
	}

	// Gather stats from the input to our HLOD build
	GatherInputStats(HLODActor, HLODRelevantComponents);
	
	const UHLODLayer* HLODLayer = HLODActor->GetSourceActors()->GetHLODLayer();
	if (!HLODLayer)
	{
		UE_LOGF(LogHLODBuilder, Error, "FWorldPartitionHLODUtilities::BuildHLOD() - Failed to find HLOD Layer");
		return false;
	}

	TSubclassOf<UHLODBuilder> HLODBuilderClass = GetHLODBuilderClass(HLODLayer);

	if (HLODBuilderClass)
	{
		UHLODBuilder* HLODBuilder = NewObject<UHLODBuilder>(GetTransientPackage(), HLODBuilderClass);
		if (ensure(HLODBuilder))
		{
			FGCObjectScopeGuard HLODBuilderGCScopeGuard(HLODBuilder);

			HLODBuilder->SetHLODBuilderSettings(HLODLayer->GetHLODBuilderSettings());

			UPackage* HLODResourcesPackage = nullptr;
			UHLODActorExternalResources* HLODActorExternalResources = nullptr;
			if (!HLODActor->GetHLODResourcesPackagePath().IsNone())
			{
				HLODResourcesPackage = FindPackage(nullptr, *HLODActor->GetHLODResourcesPackagePath().ToString());
				if (!HLODResourcesPackage)
				{
					HLODResourcesPackage = CreatePackage(*HLODActor->GetHLODResourcesPackagePath().ToString());
				}

				FString AssetName = FPackageName::GetLongPackageAssetName(HLODActor->GetHLODResourcesPackagePath().ToString());
				HLODActorExternalResources = FindObject<UHLODActorExternalResources>(HLODResourcesPackage, AssetName);
				if (!HLODActorExternalResources)
				{
					HLODActorExternalResources = NewObject<UHLODActorExternalResources>(HLODResourcesPackage, UHLODActorExternalResources::StaticClass(), FName(AssetName), RF_Public | RF_Standalone);
				}
				HLODActorExternalResources->Resources.Reset();
			}

			FHLODBuildContext HLODBuildContext;
			HLODBuildContext.TargetWorld = HLODActor->GetWorld();
			HLODBuildContext.BuildWorld = BuildHLODWorld;
			HLODBuildContext.SourceComponents = HLODRelevantComponents;
			HLODBuildContext.AssetsOuter = HLODResourcesPackage ? HLODResourcesPackage : HLODActor->GetPackage();
			HLODBuildContext.AssetsBaseName = HLODActor->GetActorLabel();
			HLODBuildContext.MinVisibleDistance = HLODActor->GetMinVisibleDistance();
			HLODBuildContext.WorldPosition = HLODActor->GetHLODBounds().GetCenter();
			HLODBuildContext.AssetsObjectFlags = HLODResourcesPackage ? (RF_Public) : RF_NoFlags;
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			HLODBuildContext.World = HLODBuildContext.TargetWorld;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

			const TSubclassOf<UWorldPartitionHLODModifier> HLODModifierClass = HLODLayer->GetHLODModifierClass();
			UWorldPartitionHLODModifier* HLODModifier = HLODModifierClass.Get() ? NewObject<UWorldPartitionHLODModifier>(GetTransientPackage(), HLODModifierClass) : nullptr;
			FGCObjectScopeGuard HLODModifierGCScopeGuard(HLODModifier);

			if (HLODModifier)
			{
				HLODModifier->BeginHLODBuild(HLODBuildContext);
			}

			// Build
			FHLODBuildResult BuildResult;
			{
				FAutoScopedDurationTimer BuildTimeScope;
				BuildResult = HLODBuilder->Build(HLODBuildContext);
				BuildTimeMS = FMath::RoundToInt(BuildTimeScope.GetTime() * 1000);
			}

			if (HLODModifier)
			{
				HLODModifier->EndHLODBuild(BuildResult.HLODComponents);
			}

			if (BuildResult.HLODComponents.IsEmpty())
			{
				UE_LOGF(LogHLODBuilder, Display, "HLOD generation created no component for HLOD actor %ls. Listing source components:", *HLODActor->GetActorLabel());
				for (UActorComponent* SourceComponent : HLODBuildContext.SourceComponents)
				{
					UE_LOGF(LogHLODBuilder, Display, "\t* %ls", *SourceComponent->GetPathName());
				}
			}

			// Ideally, this should be performed elsewhere, to allow more flexibility in the HLOD generation
			for (UActorComponent* HLODComponent : BuildResult.HLODComponents)
			{
				HLODComponent->SetCanEverAffectNavigation(false);

				if (USceneComponent* SceneComponent = Cast<USceneComponent>(HLODComponent))
				{
					// Change Mobility to be Static
					SceneComponent->SetMobility(EComponentMobility::Static);

					// Enable bounds optimizations
					SceneComponent->bComputeFastLocalBounds = true;
					SceneComponent->bComputeBoundsOnceForGame = true;
				}

				if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(HLODComponent))
				{
					// Disable collisions
					PrimitiveComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
					PrimitiveComponent->SetGenerateOverlapEvents(false);
					PrimitiveComponent->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;
					PrimitiveComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

					// HLOD visual components aren't needed on servers
					PrimitiveComponent->AlwaysLoadOnServer = false;
				}

				if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(HLODComponent))
				{
					if (UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
					{
						// Set up ray tracing far fields for always loaded HLODs (only for HLOD-created meshes)
						// or if requested by the HLOD layer (for any mesh that supports ray tracing)
						if (StaticMesh->bSupportRayTracing)
						{
							const bool bIsHLODCreatedMesh = StaticMesh->GetPackage() == HLODBuildContext.AssetsOuter;
							if ((bIsHLODCreatedMesh && !HLODActor->GetIsSpatiallyLoaded()) || HLODLayer->GetForceRayTracingFarField())
							{
								StaticMeshComponent->bRayTracingFarField = true;
							}
						}
					}
				}
			}

			// Gather assets created during the HLOD generation process
			struct FHLODAssets
			{
				TArray<UTexture*> Textures;
				TArray<UStaticMesh*> StaticMeshes;
				TArray<UMaterialInterface*> Materials;
			} HLODAssets;

			// Retrieve assets first, as some of the calls we have to perform can't be done inside of a ForEachObjectWithOuter() (ex: Rename)
			ForEachObjectWithOuter(HLODBuildContext.AssetsOuter, [&HLODAssets](UObject* Obj)
			{
				if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Obj))
				{
					HLODAssets.StaticMeshes.Add(StaticMesh);
				}

				if (UTexture* Texture = Cast<UTexture>(Obj))
				{
					HLODAssets.Textures.Add(Texture);
				}

				if (UMaterialInterface* Material = Cast<UMaterialInterface>(Obj))
				{
					HLODAssets.Materials.Add(Material);
				}
			});

			// Static meshes
			for (UStaticMesh* StaticMesh : HLODAssets.StaticMeshes)
			{
				if (HLODActorExternalResources)
				{
					HLODActorExternalResources->Resources.Add(StaticMesh);
				}

				// Disable navigation data on HLODs
				StaticMesh->MarkAsNotHavingNavigationData();

				// Ensure we can perform line trace on HLOD static meshes. This is useful for all kind of editor features on HLODs (Actor placement, Play from here, Go Here, etc)
				// Collision data will be stripped at cook.
				StaticMesh->CreateBodySetup();
				UBodySetup* BodySetup = StaticMesh->GetBodySetup();
				BodySetup->DefaultInstance.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				BodySetup->DefaultInstance.SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
				for (int32 LODIndex = 0; LODIndex < StaticMesh->GetNumLODs(); ++LODIndex)
				{
					for (int32 SectionIndex = 0; SectionIndex < StaticMesh->GetNumSections(LODIndex); ++SectionIndex)
					{
						FMeshSectionInfo MeshSectionInfo = StaticMesh->GetSectionInfoMap().Get(LODIndex, SectionIndex);
						MeshSectionInfo.bEnableCollision = true;
						StaticMesh->GetSectionInfoMap().Set(LODIndex, SectionIndex, MeshSectionInfo);
					}
				}

				// Rename owned static mesh
				StaticMesh->Rename(*MakeUniqueObjectName(StaticMesh->GetOuter(), StaticMesh->GetClass(), *FString::Printf(TEXT("StaticMesh_%s"), *HLODLayer->GetName())).ToString(), nullptr, REN_DontCreateRedirectors);
			}

			// Textures
			for (UTexture* Texture : HLODAssets.Textures)
			{
				if (HLODActorExternalResources)
				{
					HLODActorExternalResources->Resources.Add(Texture);
				}

				// Make sure all HLOD textures are assigned to the correct LODGroup
				if (Texture->LODGroup != TEXTUREGROUP_HierarchicalLOD)
				{
					Texture->PreEditChange(nullptr);
					Texture->LODGroup = TEXTUREGROUP_HierarchicalLOD;
					Texture->PostEditChange();
				}
			}

			// Materials
			for (UMaterialInterface* Material : HLODAssets.Materials)
			{
				if (HLODActorExternalResources)
				{
					HLODActorExternalResources->Resources.Add(Material);
				}
			}

			HLODActor->SetInputStats(BuildResult.InputStats);
			HLODActor->SetHLODComponents(BuildResult.HLODComponents);
		}
	}

	// Gather stats pertaining to the assets generated during this build
	GatherOutputStats(HLODActor);

	TotalTimeMS = FMath::RoundToInt(TotalTimeScope.GetTime() * 1000);

	// Build timings stats
	HLODActor->SetStat(FWorldPartitionHLODStats::BuildTimeLoadMilliseconds, LoadTimeMS);
	HLODActor->SetStat(FWorldPartitionHLODStats::BuildTimeBuildMilliseconds, BuildTimeMS);
	HLODActor->SetStat(FWorldPartitionHLODStats::BuildTimeTotalMilliseconds, TotalTimeMS);

	FHLODRebuildPolicyDataSet UpdatedRebuildPolicyDataSet = UHLODRebuildPolicy::ComputeDataForRebuildPolicies(HLODActor, HLODRelevantComponents, /*bInForComparison=*/false);
	HLODActor->SetHLODRebuildPolicyDataSet(UpdatedRebuildPolicyDataSet);

	return true;
}

void FWorldPartitionHLODUtilities::SetHLODBuildEvaluator(IWorldPartitionHLODUtilities::FHLODBuildEvaluator BuildEvaluatorDelegate)
{
	HLODBuildEvaluatorDelegate = BuildEvaluatorDelegate;
}

#endif
