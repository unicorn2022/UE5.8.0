// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithC4DImportOptions.h"

#include "DatasmithC4DTranslatorModule.h"

#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "DatasmithC4DImportPlugin"

UDatasmithC4DImportOptions::UDatasmithC4DImportOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bImportEmptyMesh = false;
	bOptimizeEmptySingleChildActors = false;
	bAlwaysGenerateNormals = false;
	ScaleVertices = 1.0;
#if WITH_EDITOR
	bExportToUDatasmith = false;
#endif //WITH_EDITOR
}

#undef LOCTEXT_NAMESPACE
