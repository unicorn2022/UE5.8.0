// Copyright Epic Games, Inc. All Rights Reserved.

#include "Mass/MetaHumanMassCrowdVisualizationTrait.h"

#include "Mass/MetaHumanCrowdAppearanceProvider.h"

#include "Mass/MetaHumanMassRepresentationSubsystem.h"
#include "MetaHumanMassCrowdRepresentationActorManagement.h"

#include "MassCrowdFragments.h"
#include "Mass/MetaHumanMassCrowdTags.h"
#include "Mass/IMetahumanMassCrowdActorBlueprintInterface.h"
#include "MassEntityTemplateRegistry.h"
#include "Mass/MetaHumanMassFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanMassCrowdVisualizationTrait)

UMetaHumanMassCrowdVisualizationTrait::UMetaHumanMassCrowdVisualizationTrait()
{
	// Override the subsystem to support parallelization of the crowd
	RepresentationSubsystemClass = UMetaHumanMassRepresentationSubsystem::StaticClass();
	Params.RepresentationActorManagementClass = UMetaHumanMassCrowdRepresentationActorManagement::StaticClass();
	Params.LODRepresentation[EMassLOD::High] = EMassRepresentationType::HighResSpawnedActor;
	Params.LODRepresentation[EMassLOD::Medium] = EMassRepresentationType::LowResSpawnedActor;
	Params.LODRepresentation[EMassLOD::Low] = EMassRepresentationType::SkinnedMeshInstance;
	Params.LODRepresentation[EMassLOD::Off] = EMassRepresentationType::None;
	// Set bKeepLowResActor to true as a spawning optimization, this will keep the low-res actor if available while showing the skinned mesh instance
	Params.bKeepLowResActors = true;
	Params.bKeepActorExtraFrame = true;
	Params.bSpreadFirstVisualizationUpdate = false;
	Params.WorldPartitionGridNameContainingCollision = NAME_None;
	Params.NotVisibleUpdateRate = 0.5f;

	LODParams.BaseLODDistance[EMassLOD::High] = 0.f;
	LODParams.BaseLODDistance[EMassLOD::Medium] = 500.f;
	LODParams.BaseLODDistance[EMassLOD::Low] = 1000.f;
	LODParams.BaseLODDistance[EMassLOD::Off] = 5000.f;

	LODParams.VisibleLODDistance[EMassLOD::High] = 0.f;
	LODParams.VisibleLODDistance[EMassLOD::Medium] = 1000.f;
	LODParams.VisibleLODDistance[EMassLOD::Low] = 5000.f;
	LODParams.VisibleLODDistance[EMassLOD::Off] = 10000.f;

	LODParams.LODMaxCount[EMassLOD::High] = 10;
	LODParams.LODMaxCount[EMassLOD::Medium] = 20;
	LODParams.LODMaxCount[EMassLOD::Low] = 500;
	LODParams.LODMaxCount[EMassLOD::Off] = TNumericLimits<int32>::Max();

	LODParams.BufferHysteresisOnDistancePercentage = 20.0f;
	LODParams.DistanceToFrustum = 0.0f;
	LODParams.DistanceToFrustumHysteresis = 0.0f;

	LODParams.FilterTag = FMassCrowdTag::StaticStruct();

	bRegisterStaticMeshDesc = false;
}

void UMetaHumanMassCrowdVisualizationTrait::SanitizeMeshParams(FMassRepresentationParameters& InOutParams, const bool bStaticMeshDeterminedInvalid, const bool bSkinnedMeshDeterminedInvalid) const
{
	// When using CharacterInstances or a procedural AppearanceProviderClass, SkinnedMeshInstanceDesc
	// may be empty because the actual mesh descs come from MHI assembly. In that case,
	// SkinnedMeshInstance is still valid.
	const bool bAppearanceComesFromMHI = !CharacterInstances.IsEmpty() || AppearanceProviderClass != nullptr;
	bool bSkinnedMeshAndCharacterInstancesInvalid = bSkinnedMeshDeterminedInvalid && !bAppearanceComesFromMHI;
	return Super::SanitizeMeshParams(InOutParams, bStaticMeshDeterminedInvalid, bSkinnedMeshAndCharacterInstancesInvalid);
}

void UMetaHumanMassCrowdVisualizationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext,
	const UWorld& World) const
{
	Super::BuildTemplate(BuildContext, World);

	UMetaHumanMassRepresentationSubsystem* RepresentationSubsystem = Cast<UMetaHumanMassRepresentationSubsystem>(World.GetSubsystemBase(RepresentationSubsystemClass));
	if (RepresentationSubsystem == nullptr && !BuildContext.IsInspectingData())
	{
		UE_LOG(LogMetaHumanMassRepresentation, Error, TEXT("Expecting a valid class for the representation subsystem"));
		RepresentationSubsystem = UWorld::GetSubsystem<UMetaHumanMassRepresentationSubsystem>(&World);
		check(RepresentationSubsystem);
	}

	if (CharacterInstances.Num() || AppearanceProviderClass != nullptr)
	{
		FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);
		FMetaHumanAppearanceSharedFragment SharedFragmentValues;
		SharedFragmentValues.ProviderClass = AppearanceProviderClass;
		if (LIKELY(BuildContext.IsInspectingData() == false))
		{
			if (CharacterInstances.Num())
			{
				SharedFragmentValues.AssignedAppearanceIndices = RepresentationSubsystem->InitializeMetaHumanInstanceRegistry(CharacterInstances);
			}

			if (AppearanceProviderClass != nullptr)
			{
				// Eagerly create the provider so its Initialize runs before any entities are spawned.
				// Pass through the already-registered CharacterInstances so the provider can use
				// them without having to re-register them.
				RepresentationSubsystem->GetOrCreateProvider(AppearanceProviderClass, CharacterInstances);
			}
		}
		BuildContext.AddSharedFragment(EntityManager.GetOrCreateSharedFragment<FMetaHumanAppearanceSharedFragment>(SharedFragmentValues));
		BuildContext.AddFragment<FMetaHumanMassIdentityFragment>();
	}

	BuildContext.AddFragment<FMassRepresentationAnimationFragment>();

	// Add animation scalability shared fragment
	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);
	FMetaHumanMassAnimationScalabilitySharedFragment ScalabilityValues;
	ScalabilityValues.SteadyStateTracksPerSequence = SteadyStateTracksPerSequence;
	ScalabilityValues.MaxBlendTracks = MaxBlendTracks;
	ScalabilityValues.AnimBlendTime = AnimBlendTime;
	BuildContext.AddSharedFragment(EntityManager.GetOrCreateSharedFragment<FMetaHumanMassAnimationScalabilitySharedFragment>(ScalabilityValues));

	BuildContext.RequireTag<FMassCrowdTag>();

	// If the high-res actor implements IMetahumanMassCrowdActorBlueprintInterface, tag these entities
	// so processors can filter on actor-driven crowd entities specifically.
	if (HighResTemplateActor != nullptr && HighResTemplateActor->ImplementsInterface(UMetahumanMassCrowdActorBlueprintInterface::StaticClass()))
	{
		BuildContext.AddTag<FMetahumanMassCrowdActorTag>();
	}
}
