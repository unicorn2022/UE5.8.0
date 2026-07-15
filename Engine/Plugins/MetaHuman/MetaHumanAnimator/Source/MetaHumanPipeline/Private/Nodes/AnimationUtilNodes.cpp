// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/AnimationUtilNodes.h"

namespace UE::MetaHuman::Pipeline
{

FAnimationMergeNode::FAnimationMergeNode(const FString& InName) : FNode("AnimationMerge", InName)
{
	Pins.Add(FPin("Animation In 1", EPinDirection::Input, EPinType::Animation, 0));
	Pins.Add(FPin("Animation In 2", EPinDirection::Input, EPinType::Animation, 1));
	Pins.Add(FPin("Animation Out", EPinDirection::Output, EPinType::Animation));
}

bool FAnimationMergeNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	const FFrameAnimationData& Animation0 = InPipelineData->GetData<FFrameAnimationData>(Pins[0]);
	const FFrameAnimationData& Animation1 = InPipelineData->GetData<FFrameAnimationData>(Pins[1]);

	FFrameAnimationData Output;

	if (Animation0.AnimationQuality == EFrameAnimationQuality::Undefined && Animation1.AnimationQuality == EFrameAnimationQuality::Undefined)
	{
		UE_LOGF(LogMetaHumanPipeline, Warning, "Both animation inputs (%ls and %ls) are invalid", *Animation0Name, *Animation1Name);
		Output = Animation0;
	}
	else if (Animation0.AnimationQuality != EFrameAnimationQuality::Undefined && Animation1.AnimationQuality == EFrameAnimationQuality::Undefined)
	{
		UE_LOGF(LogMetaHumanPipeline, Warning, "Second animation input (%ls) is invalid", *Animation1Name);
		Output = Animation0;
	}
	else if (Animation0.AnimationQuality == EFrameAnimationQuality::Undefined && Animation1.AnimationQuality != EFrameAnimationQuality::Undefined)
	{
		UE_LOGF(LogMetaHumanPipeline, Warning, "First animation input (%ls) is invalid", *Animation0Name);
		Output = Animation1;
	}
	else
	{
		Output = Animation0;

		for (const auto& Control : Animation1.AnimationData)
		{
			if (Output.AnimationData.Contains(Control.Key))
			{
				Output.AnimationData[Control.Key] = Control.Value;
			}
			else
			{
				InPipelineData->SetErrorNodeCode(ErrorCode::UnknownControlValue);
				InPipelineData->SetErrorNodeMessage(TEXT("Unknown control value: ") + Control.Key);
				return false;
			}
		}

		if (Output.AudioProcessingMode == EAudioProcessingMode::Undefined)
		{
			Output.AudioProcessingMode = Animation1.AudioProcessingMode;
		}

		Output.AnimationQuality = FMath::Min(Animation0.AnimationQuality, Animation1.AnimationQuality);
	}

	InPipelineData->SetData<FFrameAnimationData>(Pins[2], MoveTemp(Output));

	return true;
}

}
