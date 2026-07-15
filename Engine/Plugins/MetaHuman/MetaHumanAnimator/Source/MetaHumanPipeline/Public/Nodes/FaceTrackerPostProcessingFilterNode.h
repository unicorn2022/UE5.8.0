// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Pipeline/Node.h"
#include "UObject/ObjectPtr.h"
#include "DNAAsset.h"
#include "FrameAnimationData.h"
#include "MeshSolverDefinitionsType.h"

#define UE_API METAHUMANPIPELINE_API

class IFaceTrackerPostProcessingFilter;

namespace UE::MetaHuman::Pipeline
{

class FFaceTrackerPostProcessingFilterNode : public FNode
{
public:

	UE_API FFaceTrackerPostProcessingFilterNode(const FString& InName);

	UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
	UE_API virtual bool End(const TSharedPtr<FPipelineData>& InPipelineData) override;

	FString TemplateData;
	FString ConfigData;
	FString DefinitionsData;
	FString HierarchicalDefinitionsData;
	FString HierarchicalDefinitionsPlusChinCompressData;
	FString DNAFile;
	UE_DEPRECATED(5.8, "Use DNAReader instead, DNAAsset has been deprecated in favor of DNA Reader.")
	TWeakObjectPtr<UDNAAsset> DNAAsset;
	TSharedPtr<IDNAReader> DNAReader;
	TArray<FFrameAnimationData> FrameData;
	FString DebuggingFolder;
	EMeshSolverDefinitionsType MeshSolverDefinitionsType = EMeshSolverDefinitionsType::Standard;
	UE_DEPRECATED(5.8, "bSolveForTweakers has been deprecated. Use MeshSolverDefinitionsType instead.")
	bool bSolveForTweakers = false;

	enum ErrorCode
	{
		FailedToInitialize = 0
	};

protected:

	TSharedPtr<IFaceTrackerPostProcessingFilter> Filter = nullptr;
	int32 FrameNumber = 0;
};

// The managed node is a version of the above that take care of loading the correct config
// rather than these being specified by an externally.

class FFaceTrackerPostProcessingFilterManagedNode : public FFaceTrackerPostProcessingFilterNode
{
public:

	UE_API FFaceTrackerPostProcessingFilterManagedNode(const FString& InName);
};

}

#undef UE_API
