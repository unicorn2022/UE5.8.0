// Copyright Epic Games, Inc. All Rights Reserved.

#include "Preferences/MaterialEditorOptions.h"

#include "Editor/UnrealEdEngine.h"
#include "Engine/StaticMesh.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "UnrealEdGlobals.h"

UMaterialEditorOptions* UMaterialEditorOptions::Get()
{
	return GetMutableDefault<UMaterialEditorOptions>();
}

UObject* UMaterialEditorOptions::GetPreviewMesh() const
{
	UObject* PreviewMeshObject = PreviewMesh.TryLoad();

	// Fallback to legacy preview static mesh : sphere
	if (!PreviewMeshObject)
	{
		PreviewMeshObject = GUnrealEd->GetThumbnailManager()->EditorSphere;
	}

	return PreviewMeshObject;
}

void UMaterialEditorOptions::SetPreviewMesh(UObject* InPreviewMesh)
{
	if (!InPreviewMesh || PreviewMesh == InPreviewMesh)
	{
		return;
	}

	PreviewMesh = InPreviewMesh->GetPathName();
}
