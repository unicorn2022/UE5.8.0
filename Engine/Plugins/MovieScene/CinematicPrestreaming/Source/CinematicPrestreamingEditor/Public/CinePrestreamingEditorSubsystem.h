// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"
#include "CinePrestreamingEditorSubsystem.generated.h"

UCLASS(BlueprintType)
class CINEMATICPRESTREAMINGEDITOR_API UCinePrestreamingEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UCinePrestreamingEditorSubsystem() = default;

	UFUNCTION(BlueprintCallable, Category = "Cinematic Prestreaming|Editor")
	void CreatePackagesFromGeneratedData(TArray<struct FMoviePipelineCinePrestreamingGeneratedData>& InOutData);
};
