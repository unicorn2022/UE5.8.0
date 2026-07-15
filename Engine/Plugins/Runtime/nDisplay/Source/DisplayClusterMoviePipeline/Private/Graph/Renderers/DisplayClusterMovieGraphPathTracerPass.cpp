// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/Renderers/DisplayClusterMovieGraphPathTracerPass.h"

#include "Graph/DisplayClusterMovieGraphRenderCameraSource.h"
#include "Graph/Nodes/DisplayClusterMovieGraphPathTracerPassNode.h"

#include "Graph/MovieGraphConfig.h"
#include "Graph/MovieGraphDataTypes.h"
#include "Graph/MovieGraphDefaultRenderer.h"
#include "Graph/MovieGraphPipeline.h"
#include "Graph/Nodes/MovieGraphCameraNode.h"

#include "MoviePipelineUtils.h"

namespace UE::MovieGraph::Rendering
{
	void FDisplayClusterMovieGraphPathTracerPass::ModifyProjectionMatrixForTiling(const UE::MovieGraph::DefaultRenderer::FMovieGraphTilingParams& InTilingParams, const bool bInOrthographic, FMatrix& InOutProjectionMatrix, float& OutDoFSensorScale) const
	{
		if (const FDisplayClusterMovieGraphRenderCameraSource* CameraSource = static_cast<const FDisplayClusterMovieGraphRenderCameraSource*>(GetRenderCameraSource().Get()))
		{
			if (CameraSource->ModifyProjectionMatrixForTiling(GetRenderCameraIndex(), InTilingParams, bInOrthographic, InOutProjectionMatrix, OutDoFSensorScale))
			{
				return;
			}
		}

		return FMovieGraphPathTracerPass::ModifyProjectionMatrixForTiling(InTilingParams, bInOrthographic, InOutProjectionMatrix, OutDoFSensorScale);
	}

	void FDisplayClusterMovieGraphPathTracerPass::PostRendererSubmission(
		const UE::MovieGraph::FMovieGraphSampleState& InSampleState,
		const UE::MovieGraph::DefaultRenderer::FRenderTargetInitParams& InRenderTargetInitParams,
		FCanvas& InCanvas,
		const UE::MovieGraph::DefaultRenderer::FCameraInfo& InCameraInfo) const
	{
		if (const FDisplayClusterMovieGraphRenderCameraSource* CameraSource = static_cast<const FDisplayClusterMovieGraphRenderCameraSource*>(GetRenderCameraSource().Get()))
		{
			CameraSource->PostRendererSubmission(GetRenderCameraIndex(), InSampleState, InRenderTargetInitParams, InCanvas, InCameraInfo);
		}

		FMovieGraphImagePassBase::PostRendererSubmission(InSampleState, InRenderTargetInitParams, InCanvas, InCameraInfo);
	}

	void FDisplayClusterMovieGraphPathTracerPass::ApplyMovieGraphOverridesToSampleState(FMovieGraphSampleState& SampleState) const
	{
		FMovieGraphPathTracerPass::ApplyMovieGraphOverridesToSampleState(SampleState);

		if (const FDisplayClusterMovieGraphRenderCameraSource* CameraSource = static_cast<const FDisplayClusterMovieGraphRenderCameraSource*>(GetRenderCameraSource().Get()))
		{
			return CameraSource->ApplyCameraOverridesToSampleState(GetRenderCameraIndex(), RenderDataIdentifier, SampleState);
		}
	}

	UMovieGraphImagePassBaseNode* FDisplayClusterMovieGraphPathTracerPass::GetParentNode(UMovieGraphEvaluatedConfig* InConfig) const
	{
		const bool bIncludeCDOs = true;
		UDisplayClusterMovieGraphPathTracerRenderPassNode* ParentNode =
			InConfig->GetSettingForBranch<UDisplayClusterMovieGraphPathTracerRenderPassNode>(GetBranchName(), bIncludeCDOs);

		if (!ensureMsgf(ParentNode, TEXT("FDisplayClusterMovieGraphPathTracerPass should not exist without a UDisplayClusterMovieGraphPathTracerRenderPassNode in the graph.")))
		{
			return nullptr;
		}

		return ParentNode;
	}
}