// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassRepresentationSubsystem.h"
#include "MassSkinnedMeshRepresentationTypes.h"
#include "Misc/Build.h"

#include "MetaHumanMassRepresentationSubsystem.generated.h"

#define UE_API METAHUMANCROWD_API

class UMetaHumanInstance;
class UMetaHumanCrowdAppearanceProvider;
struct FMetaHumanCrowdAppearanceHandle;

DECLARE_LOG_CATEGORY_EXTERN(LogMetaHumanMassRepresentation, Log, All);

#if !UE_BUILD_SHIPPING
// Role tags consumed by the mh.Crowd.DebugISKMs overlay; string form is stable for external tooling.
namespace UE::MetaHuman::Crowd::ISKMRole
{
	inline const FName Face   = TEXT("Face");
	inline const FName Body   = TEXT("Body");
	inline const FName Groom  = TEXT("Groom");
	inline const FName Outfit = TEXT("Outfit");
}
#endif // !UE_BUILD_SHIPPING


/** Links a UMetaHumanInstance to its ISKM visualization description. */
USTRUCT()
struct FMetaHumanMassInstanceRepresentation
{
	GENERATED_BODY()

	FSkinnedMeshInstanceVisualizationDescHandle DescHandle;

	UPROPERTY()
	TObjectPtr<UMetaHumanInstance> SourceInstance;

	/** Per-instance custom-data float slices for this appearance, indexed in Desc.Meshes order. */
	TArray<TArray<float>> CustomDataFloatsPerMesh;

	/**
	 * Number of live entities currently using this appearance.
	 */
	int32 EntityRefCount = 0;

	/**
	 * If true, the provider has called UnregisterInstance and the entry will be physically
	 * released as soon as EntityRefCount reaches zero.
	 */
	bool bPendingRelease = false;

	/** True for slots that have been physically released and are awaiting reuse. */
	bool bIsFreeSlot = false;
};

/**
 * Registry of all MetaHuman instance-to-representation mappings.
 */
USTRUCT()
struct FMetaHumanMassInstanceRegistryData
{
	GENERATED_BODY()

	TArray<FMetaHumanMassInstanceRepresentation> InstanceRepresentations;
};

/**
 * Subsystem responsible for all visual of MetaHuman Mass agents.
 * 
 * Extends the engine MassRepresentationSubsystem (which now has native ISKM support)
 * with MetaHuman-specific functionality: MHI registry, appearance management, etc.
 */
UCLASS(MinimalAPI)
class UMetaHumanMassRepresentationSubsystem : public UMassRepresentationSubsystem
{
	GENERATED_BODY()

public:
	UE_API virtual void Deinitialize() override;

	/**
	 *  Get the matching ISKM visualization handle for a given appearance ID.
	 */
	FSkinnedMeshInstanceVisualizationDescHandle GetSkinnedMeshInstanceByAppearanceId(uint32 AppearanceId) const;

	/**
	 *  Get the matching MHI for a given appearance ID.
	 */
	UMetaHumanInstance* GetMetaHumanInstanceByAppearanceId(uint32 AppearanceId) const;

	TArray<uint32> InitializeMetaHumanInstanceRegistry(const TArray<UMetaHumanInstance*>& InstanceData);

	/**
	 * Per-mesh custom-data slices for the appearance, indexed in Desc.Meshes order.
	 *
	 * IMPORTANT: the returned reference points into registry storage. RegisterInstance and
	 * UnregisterInstance may reallocate that storage, invalidating the reference. Callers must
	 * not hold the reference across any subsystem mutation -- read it, use it immediately, and
	 * drop it. The empty-result reference points into a function-local static and is always safe.
	 */
	const TArray<TArray<float>>& GetCustomDataFloatsPerMesh(uint32 AppearanceId) const;

	/**
	 * Register a UMetaHumanInstance with the appearance registry. 
	 * 
	 * If the MHI hasn't been assembled yet, it will be assembled synchronously.
	 * 
	 * Repeated calls with the same MHI return the same handle.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Crowd")
	UE_API FMetaHumanCrowdAppearanceHandle RegisterInstance(UMetaHumanInstance* Instance);

	/**
	 * Mark a handle obtained from RegisterInstance as no longer needed. 
	 * 
	 * If no entities are currently using this appearance the underlying registration is released 
	 * immediately, otherwise release is deferred until the last entity carrying this appearance 
	 * is destroyed.
	 *
	 * After this call the handle is no longer guaranteed to be valid.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Crowd")
	UE_API void UnregisterInstance(FMetaHumanCrowdAppearanceHandle Handle);

	/** True if Handle currently refers to a registered, non-released appearance entry. */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Crowd")
	UE_API bool IsValidAppearanceHandle(FMetaHumanCrowdAppearanceHandle Handle) const;

	/** Returns the UMetaHumanInstance associated with Handle, or nullptr if Handle is invalid. */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Crowd")
	UE_API UMetaHumanInstance* GetInstanceForHandle(FMetaHumanCrowdAppearanceHandle Handle) const;

	/**
	 * Internal: increment the entity refcount on the given appearance entry.
	 */
	UE_API void OnEntityAssignedAppearance(uint32 AppearanceId);

	/**
	 * Internal: decrement the entity refcount on the given appearance entry.
	 */
	UE_API void OnEntityReleasedAppearance(uint32 AppearanceId);

	/**
	 * Returns the provider object for the given class, owned by this subsystem.
	 */
	UE_API UMetaHumanCrowdAppearanceProvider* GetOrCreateProvider(
		TSubclassOf<UMetaHumanCrowdAppearanceProvider> ProviderClass,
		const TArray<UMetaHumanInstance*>& PreRegisteredInstances = TArray<UMetaHumanInstance*>());

	/** Returns an existing provider for the given class without creating one. */
	UE_API UMetaHumanCrowdAppearanceProvider* GetExistingProvider(TSubclassOf<UMetaHumanCrowdAppearanceProvider> ProviderClass) const;

	/**
	 * Look up the appearance handle for an already-registered MetaHuman Instance.
	 *
	 * Returns an invalid handle if the MHI is not currently registered with the subsystem, or
	 * if the registered entry has been marked for release.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Crowd")
	UE_API FMetaHumanCrowdAppearanceHandle TryGetAppearanceHandleForInstance(UMetaHumanInstance* Instance) const;

#if !UE_BUILD_SHIPPING
	/** Role tag (Face / Body / Groom / Outfit) per Desc.Meshes entry; empty view for unknown handles. */
	TConstArrayView<FName> GetMeshRolesForDesc(FSkinnedMeshInstanceVisualizationDescHandle Handle) const;
#endif // !UE_BUILD_SHIPPING

protected:
	UPROPERTY(Transient)
	FMetaHumanMassInstanceRegistryData InstanceRegistryData;

	/** Per-mesh-Desc-hash -> per-instance custom-data float count. Source of truth for the registry-time consistency assertion. */
	TMap<uint32, int32> NumCustomDataFloatsByMeshHash;

	/** Indices into InstanceRegistryData.InstanceRepresentations whose slots have been freed and are available for reuse. */
	TArray<int32> FreeRegistrySlots;

	/** Live providers, keyed by their UClass. Owned by this subsystem. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<UClass>, TObjectPtr<UMetaHumanCrowdAppearanceProvider>> Providers;

#if !UE_BUILD_SHIPPING
	// Keyed by DescHandle.ToIndex() because FSkinnedMeshInstanceVisualizationDescHandle has no GetTypeHash.
	TMap<int32, TArray<FName>> PerDescMeshRoles;
#endif // !UE_BUILD_SHIPPING

private:
	/**
	 * Append (or recycle) a registry entry for the given MHI. Returns the registry index, or
	 * INDEX_NONE on failure (invalid MHI / no assembly output).
	 */
	int32 BuildAndAddInstanceRepresentation(UMetaHumanInstance* Instance);

	/** Locate an existing, non-free registry slot for the given MHI; INDEX_NONE if not found. */
	int32 FindRegistryIndexForInstance(const UMetaHumanInstance* Instance) const;

	/** Physically release the registry entry, removing the underlying ISKM visualization desc. */
	void ReleaseRegistryEntry(int32 RegistryIndex);
};

#undef UE_API
