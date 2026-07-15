// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMassGameModePerf.h"

#include "Engine/World.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "MassSpawner.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/NotNull.h"
#include "GameFramework/SpectatorPawn.h"
#include "Algo/Transform.h"

#include "Mass/MetaHumanMassFragments.h"
#include "MetaHumanMassSpawner.h"

namespace UE::MetaHuman::Private
{
	static FName MetaHumanCrowdTargetActor("MetaHumanCrowd.TargetActor");

	TArray<AMetaHumanMassSpawner*> GetMassSpawners(TNotNull<const UWorld*> InWorld)
	{
		TArray<AActor*> FoundActors;
		TArray<AMetaHumanMassSpawner*> Result;
		UGameplayStatics::GetAllActorsOfClass(InWorld, AMetaHumanMassSpawner::StaticClass(), FoundActors);

		Algo::Transform(
			FoundActors,
			Result,
			[](AActor* CurrentActor)
			{
				return CastChecked<AMetaHumanMassSpawner>(CurrentActor);
			});
		
		return Result;
	}

	AActor* GetTargetActor(TNotNull<const UWorld*> InWorld)
	{
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsWithTag(InWorld, MetaHumanCrowdTargetActor, FoundActors);

		if (FoundActors.IsEmpty())
		{
			return nullptr;
		}

		return FoundActors[0];
	}

}

AMetaHumanMassGameModePerf::AMetaHumanMassGameModePerf()
{
	// Use spectator pawn to avoid the sphere shadow of the default pawn.
	DefaultPawnClass = ASpectatorPawn::StaticClass();
}

void AMetaHumanMassGameModePerf::BeginPlay()
{
	// Ideally, we would use IAutomatedPerfTestInterface::SetupTest here,
	// but that means can't have automatic tests in PIE/simulate. Also,
	// this requires additional plugins and dependencies to be added, which
	// is not necessary here.
	// 
	// Otoh, BeginPlay happens only a few instructions prior to SetupTest.
	// The call is synchronous, and perf level should be in ready state for
	// the APT to start running.
	Super::BeginPlay();

	TArray<AMetaHumanMassSpawner*> MassSpawners = UE::MetaHuman::Private::GetMassSpawners(GetWorld());
	
	if (MassSpawners.IsEmpty())
	{
		return;
	}

	AActor* TargetActor = UE::MetaHuman::Private::GetTargetActor(GetWorld());

	if (!TargetActor)
	{
		return;
	}

	LastTargetLocation = TargetActor->GetActorLocation();

	for (AMetaHumanMassSpawner* MassSpawner : MassSpawners)
	{
		MassSpawner->OnSpawningFinishedEvent.AddDynamic(this, &AMetaHumanMassGameModePerf::OnSpawnCompleted);
		MassSpawner->DoSpawning();

		// Make sure that all needed assets are loaded, so APT doesn't wait
		MassSpawner->WaitForStreamingAssets();
	}
}

void AMetaHumanMassGameModePerf::OnSpawnCompleted()
{
	UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	check(EntitySubsystem);
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

	FMassEntityQuery Query(EntityManager.AsShared());
	Query.AddRequirement<FMetaHumanMassTargetLocationFragment>(EMassFragmentAccess::ReadWrite);

	FMassExecutionContext Context(EntityManager);

	// Order entities to move toward a target.
	Query.ForEachEntityChunk(Context,
		[this](FMassExecutionContext& Context)
		{
			TArrayView<FMetaHumanMassTargetLocationFragment> Targets =
				Context.GetMutableFragmentView<FMetaHumanMassTargetLocationFragment>();

			for (FMetaHumanMassTargetLocationFragment& Target : Targets)
			{
				Target.TargetLocation = LastTargetLocation;
			}
		});
}
