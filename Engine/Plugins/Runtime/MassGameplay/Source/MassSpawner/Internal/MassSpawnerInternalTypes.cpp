// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSpawnerInternalTypes.h"
#include "MassSpawnerTypes.h"
#include "MassEntityTemplateRegistry.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassSpawnerInternalTypes)

void UMassCreatedBySpawnerTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.AddFragment<FMassCreatedBySpawner>();
}
