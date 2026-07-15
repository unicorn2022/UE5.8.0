// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SkeletalMeshModelingToolsEditorModeToolkit.h"

class FSkeletalMeshModelingToolsEditorModeToolkit;
class FUICommandList;

namespace UE::SkeletalMeshModelingTools
{
	void ExtendSkeletalMeshEditorViewportToolbar(
		USkeletalMeshModelingToolsEditorMode& EdMode,
		const TSharedRef<FUICommandList>& CommandList);

	void RemoveSkeletalMeshEditorViewportToolbarExtensions();
}
