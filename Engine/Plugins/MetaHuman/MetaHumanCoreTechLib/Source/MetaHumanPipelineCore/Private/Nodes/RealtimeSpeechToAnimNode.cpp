// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/RealtimeSpeechToAnimNode.h"

#include "Pipeline/Log.h"

#include "UObject/Package.h"
#include "NNEModelData.h"

#include "SpeechAnimationSolverV4.h"

#include "GuiToRawControlsUtils.h"



TAutoConsoleVariable<FString> CVarMetaHumanRealtimeAudioBackend
{
	TEXT("mh.RealtimeAudio.Backend"),
#if PLATFORM_WINDOWS
	"NNERuntimeORTDml",
	TEXT("Controls which NNE backend is used. Tested options are \"NNERuntimeORTDml\", \"NNERuntimeORTCpu\" or \"NNERuntimeIREECpu\""),
#elif PLATFORM_LINUX
	"NNERuntimeORTCpu",
	TEXT("Controls which NNE backend is used. Tested options are \"NNERuntimeORTCpu\" or \"NNERuntimeIREECpu\""),
#elif PLATFORM_MAC
	"NNERuntimeORTCpu",
	TEXT("Controls which NNE backend is used. Tested options are \"NNERuntimeORTCpu\" or \"NNERuntimeIREECpu\""),
#else // Console
	"NNERuntimeIREECpu",
	TEXT("Controls which NNE backend is used. Tested options are \"NNERuntimeIREECpu\""),
#endif
	ECVF_Default
};

namespace UE::MetaHuman::Pipeline
{

FRealtimeSpeechToAnimNode::FRealtimeSpeechToAnimNode(const FString& InName) : FNode("RealtimeSpeechToAnimNode", InName)
{
	check(StaticEnum<EAudioDrivenAnimationMood>()->NumEnums() == 14); // 12 regular, auto-detect plus UHT added MAX item. 14 in total.

	Pins.Add(FPin("Audio In", EPinDirection::Input, EPinType::Audio));
	Pins.Add(FPin("Animation Out", EPinDirection::Output, EPinType::Animation));
}

bool FRealtimeSpeechToAnimNode::Start(const TSharedPtr<FPipelineData>& InPipelineData)
{
	if (!Solver)
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::FailedToInitialize);
		InPipelineData->SetErrorNodeMessage(TEXT("Failed to initialize"));
		return false;
	}

	Solver->ClearCache();

	return true;
}

bool FRealtimeSpeechToAnimNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	if (!Solver)
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::FailedToInitialize);
		InPipelineData->SetErrorNodeMessage(TEXT("Failed to initialize"));
		return false;
	}

	const FAudioDataType& Audio = InPipelineData->GetData<FAudioDataType>(Pins[0]);

	FSpeechAnimationAudioFrame AudioFrame;
	AudioFrame.AudioSamples = Audio.Data;
	AudioFrame.SamplesCount = Audio.NumSamples;
	AudioFrame.SampleRate = Audio.SampleRate;
	AudioFrame.NumChannels = Audio.NumChannels;
	AudioFrame.bContiguous = Audio.bContiguous;
	AudioFrame.Mood = GetMood();
	AudioFrame.MoodIntensity = GetMoodIntensity();
	AudioFrame.Lookahead = GetLookahead();

	FSpeechAnimationFrameData FrameData;
	if (!Solver->SolveAudioFrame(AudioFrame, FrameData))
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::FailedToRun);
		InPipelineData->SetErrorNodeMessage(TEXT("Failed to run"));
		return false;
	}

	check(FrameData.CurveNames.Num() == FrameData.CurveValues.Num());
	TMap<FString, float> SolverControlMap;
	for (int32 Index = 0; Index < FrameData.CurveValues.Num(); ++Index)
	{
		SolverControlMap.Add(FrameData.CurveNames[Index].ToString(), FrameData.CurveValues[Index]);
	}

	FFrameAnimationData AnimOut;
	AnimOut.AnimationData = GuiToRawControlsUtils::ConvertGuiToRawControls(SolverControlMap);
	AnimOut.AnimationQuality = EFrameAnimationQuality::PostFiltered;
	check(AnimOut.AnimationData.Num() == 251);

	InPipelineData->SetData<FFrameAnimationData>(Pins[1], MoveTemp(AnimOut));

	return true;
}

bool FRealtimeSpeechToAnimNode::End(const TSharedPtr<FPipelineData>& InPipelineData)
{
	if (!Solver)
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::FailedToInitialize);
		InPipelineData->SetErrorNodeMessage(TEXT("Failed to initialize"));
		return false;
	}

	Solver->ClearCache();

	return true;
}

bool FRealtimeSpeechToAnimNode::LoadModels()
{
	UNNEModelData* ModelData = LoadObject<UNNEModelData>(GetTransientPackage(), ISpeechAnimationSolver::GetLatestModelAssetPath());
	if (!ModelData)
	{
		UE_LOGF(LogMetaHumanPipeline, Warning, "Failed to load streaming speech to animation model");
		return false;
	}

	if (!Solver)
	{
		Solver = MakeShared<FSpeechAnimationSolverV4>(ModelData, GetNNEBackend());
	}

	if (!Solver->Initialize())
	{
		Solver = nullptr;
	}

	return Solver.IsValid();
}

void FRealtimeSpeechToAnimNode::SetMood(EAudioDrivenAnimationMood InMood)
{
	FScopeLock Lock(&MoodMutex);

	Mood = InMood;
}

EAudioDrivenAnimationMood FRealtimeSpeechToAnimNode::GetMood(void)
{
	FScopeLock Lock(&MoodMutex);

	return Mood;
}

void FRealtimeSpeechToAnimNode::SetMoodIntensity(float InMoodIntensity)
{
	FScopeLock Lock(&MoodMutex);

	MoodIntensity = InMoodIntensity;
}

float FRealtimeSpeechToAnimNode::GetMoodIntensity(void)
{
	FScopeLock Lock(&MoodMutex);

	return MoodIntensity;
}

void FRealtimeSpeechToAnimNode::SetLookahead(int32 InLookahead)
{
	FScopeLock Lock(&LookaheadMutex);

	Lookahead = InLookahead;
}

int32 FRealtimeSpeechToAnimNode::GetLookahead(void)
{
	FScopeLock Lock(&LookaheadMutex);

	return Lookahead;
}

void FRealtimeSpeechToAnimNode::SetNNEBackend(const FString& InNNEBackend)
{
	NNEBackend = InNNEBackend;
}

FString FRealtimeSpeechToAnimNode::GetNNEBackend(void) const
{
	if (NNEBackend.IsEmpty())
	{
		return CVarMetaHumanRealtimeAudioBackend.GetValueOnAnyThread();
	}
	else
	{
		return NNEBackend;
	}
}

}