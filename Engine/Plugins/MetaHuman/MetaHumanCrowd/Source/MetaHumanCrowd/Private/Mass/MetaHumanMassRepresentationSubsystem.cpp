// Copyright Epic Games, Inc. All Rights Reserved.

#include "Mass/MetaHumanMassRepresentationSubsystem.h"

#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Mass/MetaHumanCrowdAppearanceProvider.h"
#include "MassVisualizationComponent.h"
#include "MetaHumanInstance.h"
#include "MetaHumanCrowdPipeline.h"
#include "MetaHumanDefaultPipelineBase.h"
#include "MassRepresentationFragments.h"
#include "MassSkinnedMeshRepresentationTypes.h"
#include "Logging/StructuredLog.h"
#include "Misc/NotNull.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanMassRepresentationSubsystem)

DEFINE_LOG_CATEGORY(LogMetaHumanMassRepresentation);

struct FMetaHumanDefaultAssemblyOutput;

namespace UE::MetaHuman::Private
{
	// Simple helper class that collects the custom data values to be used and checks that all values registered against
	// a particular subsystem are consistent.
	class FCustomDataCollection
	{
	public:
		explicit FCustomDataCollection(TMap<uint32, int32>& ExternalRegistry) :
			Registry(ExternalRegistry)
		{
		}

		void Add(const FMassSkinnedMeshInstanceVisualizationMeshDesc& Desc, const TArray<float>& CustomData)
		{
			const uint32 MeshHash = GetTypeHash(Desc);
			const int32 NumFloats = CustomData.Num();
			TArray<float> ToAdd = CustomData;
			if (const int32* Existing = Registry.Find(MeshHash))
			{
				checkf(*Existing == NumFloats,
					TEXT("Two MHIs share an ISKMC (per-mesh Desc hash 0x%08x) but disagree on per-instance custom-data float count (existing=%d, new=%d). All appearances using the same mesh asset/materials must agree."),
					MeshHash, *Existing, NumFloats);
				if (UNLIKELY(NumFloats != *Existing))
				{
					ToAdd.SetNum(*Existing);
				}
			}
			else
			{
				Registry.Add(MeshHash, NumFloats);
			}
			Data.Add(MoveTemp(ToAdd));
		}

		TArray<TArray<float>> Data;
		TMap<uint32, int32>& Registry;
	};

	// Flattens an InstancedMaterialData TMap (keyed by material slot name) into the positional
	// MaterialOverrides array consumed by the MassRepresentation visualization pipeline.
	//
	void BuildMaterialOverridesArray(
		TNotNull<const USkinnedAsset*> Asset,
		const TMap<FName, TObjectPtr<UMaterialInterface>>& InInstancedMaterialData,
		TArray<TObjectPtr<UMaterialInterface>>& OutMaterialOverrides)
	{
		const TArray<FSkeletalMaterial>& AssetMaterials = Asset->GetMaterials();
		OutMaterialOverrides.Reset();
		OutMaterialOverrides.SetNum(AssetMaterials.Num());

		for (const TPair<FName, TObjectPtr<UMaterialInterface>>& MatDataPair : InInstancedMaterialData)
		{
			const int32 SlotIndex = AssetMaterials.IndexOfByPredicate(
				[SlotName = MatDataPair.Key](const FSkeletalMaterial& Material)
				{
					return Material.MaterialSlotName == SlotName;
				});

			if (SlotIndex == INDEX_NONE)
			{
				// Slot was authored on the source pipeline but isn't present on this fitted
				// mesh, e.g. fitted variants that carry only a subset of the source's slots.
				// Skip rather than misindex.
				UE_LOGFMT(LogMetaHumanMassRepresentation, Verbose,
					"BuildMaterialOverridesArray: slot '{Slot}' not found on asset '{Asset}'; skipping override.",
					MatDataPair.Key, Asset->GetName());
				continue;
			}

			OutMaterialOverrides[SlotIndex] = MatDataPair.Value;
		}
	}
}

void UMetaHumanMassRepresentationSubsystem::Deinitialize()
{
	for (TPair<TObjectPtr<UClass>, TObjectPtr<UMetaHumanCrowdAppearanceProvider>>& Pair : Providers)
	{
		if (Pair.Value)
		{
			Pair.Value->Shutdown(this);
		}
	}
	Providers.Reset();

	Super::Deinitialize();
}

FSkinnedMeshInstanceVisualizationDescHandle UMetaHumanMassRepresentationSubsystem::GetSkinnedMeshInstanceByAppearanceId(uint32 AppearanceId) const
{
	const int32 Index = static_cast<int32>(AppearanceId);
	if (!InstanceRegistryData.InstanceRepresentations.IsValidIndex(Index)
		|| InstanceRegistryData.InstanceRepresentations[Index].bIsFreeSlot)
	{
		return FSkinnedMeshInstanceVisualizationDescHandle();
	}
	return InstanceRegistryData.InstanceRepresentations[Index].DescHandle;
}

UMetaHumanInstance* UMetaHumanMassRepresentationSubsystem::GetMetaHumanInstanceByAppearanceId(uint32 AppearanceId) const
{
	const int32 Index = static_cast<int32>(AppearanceId);
	if (!InstanceRegistryData.InstanceRepresentations.IsValidIndex(Index)
		|| InstanceRegistryData.InstanceRepresentations[Index].bIsFreeSlot)
	{
		return nullptr;
	}
	return InstanceRegistryData.InstanceRepresentations[Index].SourceInstance.Get();
}

const TArray<TArray<float>>& UMetaHumanMassRepresentationSubsystem::GetCustomDataFloatsPerMesh(uint32 AppearanceId) const
{
	const int32 Index = static_cast<int32>(AppearanceId);
	if (!InstanceRegistryData.InstanceRepresentations.IsValidIndex(Index)
		|| InstanceRegistryData.InstanceRepresentations[Index].bIsFreeSlot)
	{
		static const TArray<TArray<float>> EmptyResult;
		return EmptyResult;
	}
	return InstanceRegistryData.InstanceRepresentations[Index].CustomDataFloatsPerMesh;
}

#if !UE_BUILD_SHIPPING
TConstArrayView<FName> UMetaHumanMassRepresentationSubsystem::GetMeshRolesForDesc(FSkinnedMeshInstanceVisualizationDescHandle Handle) const
{
	if (!Handle.IsValid())
	{
		return {};
	}
	if (const TArray<FName>* Found = PerDescMeshRoles.Find(Handle.ToIndex()))
	{
		return *Found;
	}
	return {};
}

#endif // !UE_BUILD_SHIPPING

int32 UMetaHumanMassRepresentationSubsystem::FindRegistryIndexForInstance(const UMetaHumanInstance* Instance) const
{
	for (int32 Index = 0; Index < InstanceRegistryData.InstanceRepresentations.Num(); ++Index)
	{
		const FMetaHumanMassInstanceRepresentation& Entry = InstanceRegistryData.InstanceRepresentations[Index];
		if (!Entry.bIsFreeSlot && Entry.SourceInstance == Instance)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 UMetaHumanMassRepresentationSubsystem::BuildAndAddInstanceRepresentation(UMetaHumanInstance* Instance)
{
	if (!Instance)
	{
		UE_LOGFMT(LogMetaHumanMassRepresentation, Error, "Invalid MHI passed to BuildAndAddInstanceRepresentation");
		return INDEX_NONE;
	}

	if (!Instance->GetAssemblyOutput().IsValid())
	{
		UE_LOGFMT(LogMetaHumanMassRepresentation, Error, "No MHI Assembly output available for {InstanceName}", Instance->GetName());
		return INDEX_NONE;
	}

	const FMetaHumanCrowdAssemblyOutput* AssemblyOutput = Instance->GetAssemblyOutput().GetPtr<FMetaHumanCrowdAssemblyOutput>();
	if (!AssemblyOutput)
	{
		return INDEX_NONE;
	}

	// Build an instance description from the Instance data
	FSkinnedMeshInstanceVisualizationDesc MetaHumanSkinnedMeshInstanceDesc;

	// All mesh parts must always animate regardless of screen size so that multi-part
	// characters stay in sync and motion vectors are set correctly on all parts.
	constexpr float AlwaysAnimate = -1.0f;

	UE::MetaHuman::Private::FCustomDataCollection CustomData(NumCustomDataFloatsByMeshHash);

#if !UE_BUILD_SHIPPING
	TArray<FName> PerMeshRoles;
#endif // !UE_BUILD_SHIPPING

	if (AssemblyOutput->InstancedFaceMesh)
	{
		FMassSkinnedMeshInstanceVisualizationMeshDesc FaceMeshDescription;
		FaceMeshDescription.bCastShadows = true;
		FaceMeshDescription.AnimationMinScreenSize = AlwaysAnimate;
		FaceMeshDescription.Asset = AssemblyOutput->InstancedFaceMesh;
		FaceMeshDescription.TransformProvider = AssemblyOutput->InstancedFaceMeshTransformProvider;
		UE::MetaHuman::Private::BuildMaterialOverridesArray(
			AssemblyOutput->InstancedFaceMesh,
			AssemblyOutput->InstancedFaceMaterialOverrides,
			FaceMeshDescription.MaterialOverrides);

		MetaHumanSkinnedMeshInstanceDesc.Meshes.Add(FaceMeshDescription);
		CustomData.Add(FaceMeshDescription, AssemblyOutput->InstancedFaceMeshCustomDataFloats);
#if !UE_BUILD_SHIPPING
		PerMeshRoles.Add(UE::MetaHuman::Crowd::ISKMRole::Face);
#endif // !UE_BUILD_SHIPPING
	}

	if (AssemblyOutput->bIsBodyMeshVisible && AssemblyOutput->InstancedBodyMesh)
	{
		FMassSkinnedMeshInstanceVisualizationMeshDesc BodyMeshDescription;
		BodyMeshDescription.bCastShadows = true;
		BodyMeshDescription.AnimationMinScreenSize = AlwaysAnimate;
		BodyMeshDescription.Asset = AssemblyOutput->InstancedBodyMesh;
		BodyMeshDescription.TransformProvider = AssemblyOutput->InstancedBodyMeshTransformProvider;
		MetaHumanSkinnedMeshInstanceDesc.Meshes.Add(BodyMeshDescription);
		CustomData.Add(BodyMeshDescription, {});
#if !UE_BUILD_SHIPPING
		PerMeshRoles.Add(UE::MetaHuman::Crowd::ISKMRole::Body);
#endif // !UE_BUILD_SHIPPING
	}

	// Create grooms as ISKM
	for (const FMetaHumanCrowdInstancedGroomAssemblyOutput& GroomAssembly : AssemblyOutput->InstancedGrooms)
	{
		if (GroomAssembly.CardsMesh)
		{
			FMassSkinnedMeshInstanceVisualizationMeshDesc GroomMeshDescription;
			GroomMeshDescription.bCastShadows = true;
			GroomMeshDescription.AnimationMinScreenSize = AlwaysAnimate;
			GroomMeshDescription.Asset = GroomAssembly.CardsMesh;
			GroomMeshDescription.TransformProvider = GroomAssembly.TransformProvider;
			UE::MetaHuman::Private::BuildMaterialOverridesArray(
				GroomAssembly.CardsMesh,
				GroomAssembly.InstancedMaterialData,
				GroomMeshDescription.MaterialOverrides);
			MetaHumanSkinnedMeshInstanceDesc.Meshes.Add(GroomMeshDescription);
			CustomData.Add(GroomMeshDescription, GroomAssembly.InstancedMeshCustomDataFloats);
#if !UE_BUILD_SHIPPING
			PerMeshRoles.Add(UE::MetaHuman::Crowd::ISKMRole::Groom);
#endif // !UE_BUILD_SHIPPING
		}
	}

	for (const FMetaHumanCrowdInstancedClothingAssemblyOutput& ClothingInfo : AssemblyOutput->InstancedClothing)
	{
		if (ClothingInfo.OutfitMesh)
		{
			FMassSkinnedMeshInstanceVisualizationMeshDesc OutfitPartDescription;
			OutfitPartDescription.bCastShadows = true;
			OutfitPartDescription.AnimationMinScreenSize = AlwaysAnimate;
			OutfitPartDescription.Asset = ClothingInfo.OutfitMesh;
			OutfitPartDescription.TransformProvider = ClothingInfo.TransformProvider;
			UE::MetaHuman::Private::BuildMaterialOverridesArray(
				ClothingInfo.OutfitMesh,
				ClothingInfo.InstancedMaterialData,
				OutfitPartDescription.MaterialOverrides);
			MetaHumanSkinnedMeshInstanceDesc.Meshes.Add(OutfitPartDescription);
			CustomData.Add(OutfitPartDescription, ClothingInfo.InstancedMeshCustomDataFloats);
#if !UE_BUILD_SHIPPING
			PerMeshRoles.Add(UE::MetaHuman::Crowd::ISKMRole::Outfit);
#endif // !UE_BUILD_SHIPPING
		}
	}

	MetaHumanSkinnedMeshInstanceDesc.TransformOffset = FTransform(FRotator(0, -90, 0));
	MetaHumanSkinnedMeshInstanceDesc.bUseTransformOffset = true;

	FMetaHumanMassInstanceRepresentation InstanceRepresentation;
	InstanceRepresentation.DescHandle = FindOrAddSkinnedMeshVisualizationDesc(MetaHumanSkinnedMeshInstanceDesc);
	InstanceRepresentation.SourceInstance = Instance;
	InstanceRepresentation.CustomDataFloatsPerMesh = MoveTemp(CustomData.Data);

#if !UE_BUILD_SHIPPING
	PerDescMeshRoles.FindOrAdd(InstanceRepresentation.DescHandle.ToIndex()) = MoveTemp(PerMeshRoles);
#endif // !UE_BUILD_SHIPPING

	// Insert into a free slot if available, otherwise append.
	int32 NewIndex;
	if (FreeRegistrySlots.Num() > 0)
	{
		NewIndex = FreeRegistrySlots.Pop(EAllowShrinking::No);
		check(InstanceRegistryData.InstanceRepresentations.IsValidIndex(NewIndex));
		InstanceRegistryData.InstanceRepresentations[NewIndex] = MoveTemp(InstanceRepresentation);
	}
	else
	{
		NewIndex = InstanceRegistryData.InstanceRepresentations.Add(MoveTemp(InstanceRepresentation));
	}
	return NewIndex;
}

void UMetaHumanMassRepresentationSubsystem::ReleaseRegistryEntry(int32 RegistryIndex)
{
	if (!InstanceRegistryData.InstanceRepresentations.IsValidIndex(RegistryIndex))
	{
		return;
	}

	FMetaHumanMassInstanceRepresentation& Entry = InstanceRegistryData.InstanceRepresentations[RegistryIndex];
	if (Entry.bIsFreeSlot)
	{
		return;
	}

	// Suppress the physical engine-side release if the world is tearing down or the visualization
	// component has already been torn down. 
	//
	// During world teardown the inner ISKMCs may have been unregistered already, so calling
	// RemoveSkinnedMeshInstanceVisualDesc would trip its internal checks. 
	//
	// Our bookkeeping is still cleared below so the registry stays consistent if anything tries to
	// use it again.
	UWorld* World = GetWorld();
	const bool bWorldGone = World == nullptr || World->bIsTearingDown;
	UMassVisualizationComponent* VisComp = bWorldGone ? nullptr : GetVisualizationComponent();

	if (VisComp != nullptr && Entry.DescHandle.IsValid())
	{
		VisComp->RemoveSkinnedMeshInstanceVisualDesc(Entry.DescHandle);
	}

#if !UE_BUILD_SHIPPING
	if (Entry.DescHandle.IsValid())
	{
		PerDescMeshRoles.Remove(Entry.DescHandle.ToIndex());
	}
#endif // !UE_BUILD_SHIPPING

	Entry = FMetaHumanMassInstanceRepresentation();
	Entry.bIsFreeSlot = true;
	FreeRegistrySlots.Add(RegistryIndex);
}

TArray<uint32> UMetaHumanMassRepresentationSubsystem::InitializeMetaHumanInstanceRegistry(const TArray<UMetaHumanInstance*>& InstanceData)
{
	TArray<uint32> UsedIds;
	for (UMetaHumanInstance* Instance : InstanceData)
	{
		if (!Instance)
		{
			UE_LOGFMT(LogMetaHumanMassRepresentation, Error, "Invalid MHI in InstanceData array");
			continue;
		}

		if (!Instance->GetAssemblyOutput().IsValid())
		{
			UE_LOGFMT(LogMetaHumanMassRepresentation, Error, "No MHI Assembly output available");
			continue;
		}

		const int32 ExistingIndex = FindRegistryIndexForInstance(Instance);
		if (ExistingIndex != INDEX_NONE)
		{
			UsedIds.Add(static_cast<uint32>(ExistingIndex));
			continue;
		}

		const int32 NewIndex = BuildAndAddInstanceRepresentation(Instance);
		if (NewIndex != INDEX_NONE)
		{
			UsedIds.Add(static_cast<uint32>(NewIndex));
		}
	}
	return UsedIds;
}

FMetaHumanCrowdAppearanceHandle UMetaHumanMassRepresentationSubsystem::RegisterInstance(UMetaHumanInstance* Instance)
{
	if (!Instance)
	{
		UE_LOGFMT(LogMetaHumanMassRepresentation, Error, "RegisterInstance: invalid MHI");
		return FMetaHumanCrowdAppearanceHandle();
	}

	if (!Instance->GetAssemblyOutput().IsValid())
	{
		UE_LOGFMT(LogMetaHumanMassRepresentation, Error, "RegisterInstance: MHI {InstanceName} failed to assemble", Instance->GetName());
		return FMetaHumanCrowdAppearanceHandle();
	}

	int32 ExistingIndex = FindRegistryIndexForInstance(Instance);
	if (ExistingIndex != INDEX_NONE)
	{
		// Re-registering: clear any pending-release so a previously-marked entry stays alive.
		FMetaHumanMassInstanceRepresentation& Entry = InstanceRegistryData.InstanceRepresentations[ExistingIndex];
		Entry.bPendingRelease = false;
		return FMetaHumanCrowdAppearanceHandle(static_cast<uint32>(ExistingIndex));
	}

	const int32 NewIndex = BuildAndAddInstanceRepresentation(Instance);
	if (NewIndex == INDEX_NONE)
	{
		UE_LOGFMT(LogMetaHumanMassRepresentation, Error,
			"RegisterInstance: BuildAndAddInstanceRepresentation failed for MHI {InstanceName}. "
			"Returning invalid handle.", Instance->GetName());
		return FMetaHumanCrowdAppearanceHandle();
	}

	UE_LOGFMT(LogMetaHumanMassRepresentation, Verbose,
		"RegisterInstance: registered MHI {InstanceName} as appearance index {Index}.",
		Instance->GetName(), NewIndex);
	return FMetaHumanCrowdAppearanceHandle(static_cast<uint32>(NewIndex));
}

void UMetaHumanMassRepresentationSubsystem::UnregisterInstance(FMetaHumanCrowdAppearanceHandle Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}

	const int32 Index = static_cast<int32>(Handle.GetIndex());
	if (!InstanceRegistryData.InstanceRepresentations.IsValidIndex(Index))
	{
		return;
	}

	FMetaHumanMassInstanceRepresentation& Entry = InstanceRegistryData.InstanceRepresentations[Index];
	if (Entry.bIsFreeSlot)
	{
		return;
	}

	Entry.bPendingRelease = true;
	if (Entry.EntityRefCount <= 0)
	{
		ReleaseRegistryEntry(Index);
	}
}

bool UMetaHumanMassRepresentationSubsystem::IsValidAppearanceHandle(FMetaHumanCrowdAppearanceHandle Handle) const
{
	if (!Handle.IsValid())
	{
		return false;
	}

	const int32 Index = static_cast<int32>(Handle.GetIndex());
	if (!InstanceRegistryData.InstanceRepresentations.IsValidIndex(Index))
	{
		return false;
	}

	return !InstanceRegistryData.InstanceRepresentations[Index].bIsFreeSlot;
}

UMetaHumanInstance* UMetaHumanMassRepresentationSubsystem::GetInstanceForHandle(FMetaHumanCrowdAppearanceHandle Handle) const
{
	if (!IsValidAppearanceHandle(Handle))
	{
		return nullptr;
	}

	return InstanceRegistryData.InstanceRepresentations[static_cast<int32>(Handle.GetIndex())].SourceInstance.Get();
}

void UMetaHumanMassRepresentationSubsystem::OnEntityAssignedAppearance(uint32 AppearanceId)
{
	const int32 Index = static_cast<int32>(AppearanceId);
	if (!InstanceRegistryData.InstanceRepresentations.IsValidIndex(Index))
	{
		return;
	}

	FMetaHumanMassInstanceRepresentation& Entry = InstanceRegistryData.InstanceRepresentations[Index];
	if (Entry.bIsFreeSlot)
	{
		return;
	}

	++Entry.EntityRefCount;
}

void UMetaHumanMassRepresentationSubsystem::OnEntityReleasedAppearance(uint32 AppearanceId)
{
	const int32 Index = static_cast<int32>(AppearanceId);
	if (!InstanceRegistryData.InstanceRepresentations.IsValidIndex(Index))
	{
		return;
	}

	FMetaHumanMassInstanceRepresentation& Entry = InstanceRegistryData.InstanceRepresentations[Index];
	if (Entry.bIsFreeSlot)
	{
		return;
	}

	--Entry.EntityRefCount;
	checkf(Entry.EntityRefCount >= 0, TEXT("MetaHuman crowd appearance refcount went negative for index %d"), Index);
	
	if (Entry.bPendingRelease && Entry.EntityRefCount <= 0)
	{
		ReleaseRegistryEntry(Index);
	}
}

UMetaHumanCrowdAppearanceProvider* UMetaHumanMassRepresentationSubsystem::GetOrCreateProvider(
	TSubclassOf<UMetaHumanCrowdAppearanceProvider> ProviderClass,
	const TArray<UMetaHumanInstance*>& PreRegisteredInstances)
{
	if (!ProviderClass)
	{
		return nullptr;
	}

	UClass* Key = ProviderClass.Get();
	if (TObjectPtr<UMetaHumanCrowdAppearanceProvider>* Existing = Providers.Find(Key))
	{
		// Provider already exists; PreRegisteredInstances is ignored on subsequent calls
		// because Initialize fires exactly once.
		return *Existing;
	}

	UMetaHumanCrowdAppearanceProvider* NewProvider = NewObject<UMetaHumanCrowdAppearanceProvider>(this, ProviderClass);
	Providers.Add(Key, NewProvider);
	NewProvider->Initialize(this, PreRegisteredInstances);
	return NewProvider;
}

UMetaHumanCrowdAppearanceProvider* UMetaHumanMassRepresentationSubsystem::GetExistingProvider(TSubclassOf<UMetaHumanCrowdAppearanceProvider> ProviderClass) const
{
	if (!ProviderClass)
	{
		return nullptr;
	}

	const TObjectPtr<UMetaHumanCrowdAppearanceProvider>* Existing = Providers.Find(ProviderClass.Get());
	return Existing ? Existing->Get() : nullptr;
}

FMetaHumanCrowdAppearanceHandle UMetaHumanMassRepresentationSubsystem::TryGetAppearanceHandleForInstance(UMetaHumanInstance* Instance) const
{
	if (!IsValid(Instance))
	{
		return FMetaHumanCrowdAppearanceHandle();
	}

	const int32 Index = FindRegistryIndexForInstance(Instance);
	if (Index == INDEX_NONE)
	{
		return FMetaHumanCrowdAppearanceHandle();
	}

	const FMetaHumanMassInstanceRepresentation& Entry = InstanceRegistryData.InstanceRepresentations[Index];
	if (Entry.bPendingRelease)
	{
		return FMetaHumanCrowdAppearanceHandle();
	}

	return FMetaHumanCrowdAppearanceHandle(static_cast<uint32>(Index));
}
