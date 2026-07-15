// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AdvancedPreviewScene.h"
#include "EditorViewportClient.h"

namespace UE::Workspace
{
	class FWorkspaceAssetViewportClient : public FEditorViewportClient
	{
	public:
		FWorkspaceAssetViewportClient(FEditorModeTools* InModeTools, const TSharedPtr<FAdvancedPreviewScene>& InPreviewScene)
			: FEditorViewportClient(InModeTools, InPreviewScene.Get())
			, AdvancedPreviewScene(InPreviewScene)
		{
		}
		
		// FEditorViewportClient interface
		virtual void Tick(float DeltaTime) override;
		
		TSharedPtr<FAdvancedPreviewScene> AdvancedPreviewScene = nullptr;
	};
}