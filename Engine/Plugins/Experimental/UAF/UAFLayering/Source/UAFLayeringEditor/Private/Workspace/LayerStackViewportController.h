// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UAFLayerStack.h"
#include "WorkspaceViewportController.h"
#include "Engine/SkeletalMesh.h"
#include "Module/AnimNextModule.h"

namespace UE::UAF::LayeringEditor
{
	class FLayerStackViewportController : public Workspace::IWorkspaceViewportController
	{
	public:
		virtual void OnEnter(const FViewportContext& InViewportContext) override;
		virtual void OnExit(FAdvancedPreviewScene* PreviewScene) override;

	private:
		void AddMeshToPreview(FAdvancedPreviewScene* PreviewScene, const TObjectPtr<const UUAFSystem> System, const TObjectPtr<const UUAFLayerStack> LayerStack, USkeletalMesh* InSkeletalMesh);
		
		TArray<AActor*> PreviewActors;
	};
}
