// Copyright Epic Games, Inc. All Rights Reserved.

#include "Mass/MetaHumanCrowdAppearanceProvider.h"

#include "Mass/MetaHumanMassRepresentationSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanCrowdAppearanceProvider)

void UMetaHumanCrowdAppearanceProvider::Initialize_Implementation(UMetaHumanMassRepresentationSubsystem* /*Subsystem*/, const TArray<UMetaHumanInstance*>& /*PreRegisteredInstances*/)
{
}

FMetaHumanCrowdAppearanceHandle UMetaHumanCrowdAppearanceProvider::AcquireAppearance_Implementation(UMetaHumanMassRepresentationSubsystem* /*Subsystem*/, const FVector& /*SpawnLocation*/)
{
	return FMetaHumanCrowdAppearanceHandle();
}

void UMetaHumanCrowdAppearanceProvider::OnEntityDespawned_Implementation(UMetaHumanMassRepresentationSubsystem* /*Subsystem*/, FMetaHumanCrowdAppearanceHandle /*Handle*/)
{
}

void UMetaHumanCrowdAppearanceProvider::Shutdown_Implementation(UMetaHumanMassRepresentationSubsystem* /*Subsystem*/)
{
}
