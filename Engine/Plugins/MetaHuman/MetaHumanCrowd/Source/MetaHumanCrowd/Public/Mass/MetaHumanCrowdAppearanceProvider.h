// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "MetaHumanCrowdAppearanceProvider.generated.h"

#define UE_API METAHUMANCROWD_API

class UMetaHumanInstance;
class UMetaHumanMassRepresentationSubsystem;

/**
 * Opaque handle to an appearance registered with UMetaHumanMassRepresentationSubsystem.
 */
USTRUCT(BlueprintType)
struct FMetaHumanCrowdAppearanceHandle
{
	GENERATED_BODY()

	static constexpr uint32 InvalidIndex = TNumericLimits<uint32>::Max();

	FMetaHumanCrowdAppearanceHandle() = default;

	explicit FMetaHumanCrowdAppearanceHandle(uint32 InIndex)
		: Index(InIndex)
	{
	}

	bool IsValid() const
	{
		return Index != InvalidIndex;
	}

	uint32 GetIndex() const
	{
		return Index;
	}

	bool operator==(const FMetaHumanCrowdAppearanceHandle& Other) const = default;

	friend uint32 GetTypeHash(const FMetaHumanCrowdAppearanceHandle& Handle)
	{
		return ::GetTypeHash(Handle.Index);
	}

private:
	UPROPERTY()
	uint32 Index = InvalidIndex;
};


/**
 * Base class for objects that decide which appearance is used for each spawned crowd entity.
 *
 * Subclass this (in C++ or Blueprint) and assign your subclass to the AppearanceProviderClass
 * property on the MetaHuman Crowd Visualization trait. 
 *
 * The same provider object handles every spawn/despawn for that class in that world, so it is safe
 * to hold persistent state.
 *
 * Typical usage:
 *
 *   1. Override Initialize to inspect the pre-registered MHIs and assemble any extra MHIs you want 
 * 		to make available. Call Subsystem->RegisterInstance(MyAssembledInstance) for each new MHI.
 *
 *   2. Override AcquireAppearance to pick an appearance for a new entity. 
 *
 *		Use Subsystem->TryGetAppearanceHandleForInstance(MHI) to look up the handle for any MHI you
 *      already know about (whether pre-registered or registered by this provider). 
 *
 *		The world location of the spawn is provided so you can vary appearance by location.
 *
 *   3. Override OnEntityDespawned to update bookkeeping when an entity is destroyed. This fires
 *      on actual entity destruction, not on temporary LOD-driven invisibility.
 *
 *   4. To grow the pool at runtime, create a new MHI and call RegisterInstance.
 *
 *      To shrink the pool, call UnregisterInstance(Handle). The subsystem refcounts handles, so
 *      a handle that is still in use by entities will be physically released only when the last
 *      such entity is destroyed.
 */
UCLASS(MinimalAPI, Blueprintable, EditInlineNew, Abstract)
class UMetaHumanCrowdAppearanceProvider : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Called once when this provider is first created for a world.
	 *
	 * The PreRegisteredInstances array contains the MetaHuman Instances that were already
	 * registered with the subsystem before this provider was created -- typically the contents
	 * of the trait's CharacterInstances property. To obtain their handles call 
	 * Subsystem->TryGetAppearanceHandleForInstance(MHI).
	 *
	 * Use this opportunity to assemble any additional MHIs you want to add to the pool, and
	 * call Subsystem->RegisterInstance for each.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MetaHuman|Crowd")
	void Initialize(UMetaHumanMassRepresentationSubsystem* Subsystem, const TArray<UMetaHumanInstance*>& PreRegisteredInstances);
	UE_API virtual void Initialize_Implementation(UMetaHumanMassRepresentationSubsystem* Subsystem, const TArray<UMetaHumanInstance*>& PreRegisteredInstances);

	/**
	 * Called for each new crowd entity to determine which appearance it should use.
	 *
	 * @param Subsystem    The owning representation subsystem, useful for registering more
	 *                     instances on demand.
	 * @param SpawnLocation World-space location at which the entity is being spawned.
	 * @return             A handle previously obtained from RegisterInstance. Returning an
	 *                     invalid handle falls back to the trait's CharacterInstances pool if
	 *                     one is set, or leaves the entity with no appearance otherwise.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MetaHuman|Crowd")
	FMetaHumanCrowdAppearanceHandle AcquireAppearance(UMetaHumanMassRepresentationSubsystem* Subsystem, const FVector& SpawnLocation);
	UE_API virtual FMetaHumanCrowdAppearanceHandle AcquireAppearance_Implementation(UMetaHumanMassRepresentationSubsystem* Subsystem, const FVector& SpawnLocation);

	/**
	 * Called when an entity that was assigned an appearance by this provider is despawned.
	 *
	 * Does not fire for temporary LOD-driven invisibility, only when the entity itself is
	 * destroyed.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MetaHuman|Crowd")
	void OnEntityDespawned(UMetaHumanMassRepresentationSubsystem* Subsystem, FMetaHumanCrowdAppearanceHandle Handle);
	UE_API virtual void OnEntityDespawned_Implementation(UMetaHumanMassRepresentationSubsystem* Subsystem, FMetaHumanCrowdAppearanceHandle Handle);

	/**
	 * Called when this provider's owning subsystem is being torn down.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MetaHuman|Crowd")
	void Shutdown(UMetaHumanMassRepresentationSubsystem* Subsystem);
	UE_API virtual void Shutdown_Implementation(UMetaHumanMassRepresentationSubsystem* Subsystem);
};

#undef UE_API
