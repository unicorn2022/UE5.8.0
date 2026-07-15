// Copyright Epic Games, Inc. All Rights Reserved.


#include "EditorViewportCameraProvider.h"

#include "Editor.h"
#include "Subsystems/UnrealEditorSubsystem.h"

bool FEditorViewportCameraProvider::GetCamera(FVector& OutLocation, FRotator& OutRotation) const
{
	if (!GEditor) return false;

	// If we're playing (PIE), fallback to runtime behavior
	if (GEditor->GetPIEWorldContext())
	{
		static FRuntimeCameraProvider DefaultProvider;
		
		return DefaultProvider.GetCamera(OutLocation, OutRotation);
	}

	if (UUnrealEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>())
	{
		return EditorSubsystem->GetLevelViewportCameraInfo(OutLocation, OutRotation);
	}
	return false;
}