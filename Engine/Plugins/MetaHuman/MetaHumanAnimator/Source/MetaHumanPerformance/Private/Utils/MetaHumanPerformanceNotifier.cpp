// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utils/MetaHumanPerformanceNotifier.h"

#include "MetaHumanPerformance.h"
#include "Pipeline/PipelineData.h"
#include "Pipeline/DataTreeTypes.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "HAL/PlatformTime.h"
#include "Internationalization/Text.h"
#include "Misc/Timespan.h"

#include "OutputLogModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "MetaHumanPerformanceNotifier"

class FMetaHumanPerformanceNotifier::FImpl
{
public:

	FImpl(UMetaHumanPerformance* InPerformance);
	~FImpl();

	void Bind();
	void Unbind();

	void StartShowing();

private:

	// Delegate handlers
	void HandleFrameProcessed(int32 InFrameNumber);
	void HandleStageFinished(int32 InStageCompleted);
	void HandleProcessingFinished(TSharedPtr<const UE::MetaHuman::Pipeline::FPipelineData> InPipelineData);

	// Notification helpers
	void UpdateProgress(int32 InFrameNumber);
	void FinishSuccess(const FText& InMessage);
	void FinishFailure(const FText& InMessage);

	void UpdateLocalValues();

	TWeakObjectPtr<UMetaHumanPerformance> PerformanceWeak;
	TSharedPtr<SNotificationItem> ActiveItem;

	FDelegateHandle FrameProcessedHandle;
	FDelegateHandle StageFinishedHandle;
	FDelegateHandle ProcessingFinishedHandle;

	TArray<FFrameRange> CachedFrameRanges;
	int32 CachedFrameRangeIndex = -1;
	int32 CurrentPipelineStage = -1;
	int32 TotalPipelineStages = -1;
	bool bShouldUpdateLocals = false;

	double LastUpdateTime = 0.0;
	double StartTime = 0.0;

	/** Throttle on-screen text updates; per-frame churn is wasted Slate work. */
	static constexpr double UpdateThrottleSeconds = 0.5;
};

TSharedRef<FMetaHumanPerformanceNotifier> FMetaHumanPerformanceNotifier::Attach(UMetaHumanPerformance* InPerformance)
{
	TSharedRef<FMetaHumanPerformanceNotifier> Notifier = MakeShared<FMetaHumanPerformanceNotifier>(InPerformance, FPrivateToken());
	Notifier->Impl->Bind();

	return Notifier;
}

FMetaHumanPerformanceNotifier::FMetaHumanPerformanceNotifier(UMetaHumanPerformance* InPerformance, FPrivateToken)
	: Impl(MakePimpl<FImpl>(InPerformance))
{
}

FMetaHumanPerformanceNotifier::~FMetaHumanPerformanceNotifier() = default;

void FMetaHumanPerformanceNotifier::StartShowing()
{
	Impl->StartShowing();
}

FMetaHumanPerformanceNotifier::FImpl::FImpl(UMetaHumanPerformance* InPerformance)
	: PerformanceWeak(InPerformance)
	, StartTime(FPlatformTime::Seconds())
{
}

FMetaHumanPerformanceNotifier::FImpl::~FImpl()
{
	Unbind();

	if (ActiveItem.IsValid() && ActiveItem->GetCompletionState() == SNotificationItem::CS_Pending)
	{
		ActiveItem->SetText(LOCTEXT("Cancelled", "MetaHuman Performance processing cancelled."));
		ActiveItem->SetCompletionState(SNotificationItem::CS_Fail);
		ActiveItem->ExpireAndFadeout();
		ActiveItem.Reset();
	}
}

void FMetaHumanPerformanceNotifier::FImpl::Bind()
{
	UMetaHumanPerformance* Performance = PerformanceWeak.Get();
	if (!Performance)
	{
		return;
	}

	FrameProcessedHandle = Performance->OnFrameProcessed().AddRaw(this, &FMetaHumanPerformanceNotifier::FImpl::HandleFrameProcessed);
	StageFinishedHandle = Performance->OnStageProcessingFinished().AddRaw(this, &FMetaHumanPerformanceNotifier::FImpl::HandleStageFinished);
	ProcessingFinishedHandle = Performance->OnProcessingFinished().AddRaw(this, &FMetaHumanPerformanceNotifier::FImpl::HandleProcessingFinished);
}

void FMetaHumanPerformanceNotifier::FImpl::Unbind()
{
	UMetaHumanPerformance* Performance = PerformanceWeak.Get();
	if (!Performance)
	{
		return;
	}

	if (FrameProcessedHandle.IsValid())
	{
		Performance->OnFrameProcessed().Remove(FrameProcessedHandle);
		FrameProcessedHandle.Reset();
	}
	if (StageFinishedHandle.IsValid())
	{
		Performance->OnStageProcessingFinished().Remove(StageFinishedHandle);
		StageFinishedHandle.Reset();
	}
	if (ProcessingFinishedHandle.IsValid())
	{
		Performance->OnProcessingFinished().Remove(ProcessingFinishedHandle);
		ProcessingFinishedHandle.Reset();
	}
}

void FMetaHumanPerformanceNotifier::FImpl::UpdateLocalValues()
{
	const UMetaHumanPerformance* Performance = PerformanceWeak.Get();
	if (!Performance)
	{
		return;
	}

	CachedFrameRangeIndex = Performance->GetPipelineFrameRange();
	CurrentPipelineStage = Performance->GetPipelineStage() + 1 + (CachedFrameRangeIndex * Performance->GetTotalPipelineStage()); // Next stage
}

void FMetaHumanPerformanceNotifier::FImpl::StartShowing()
{
	FNotificationInfo Info(LOCTEXT("Starting", "Starting MetaHuman Performance pipeline..."));
	Info.bFireAndForget = false;
	Info.bUseSuccessFailIcons = true;
	Info.bUseThrobber = true;
	Info.bUseLargeFont = false;
	Info.FadeOutDuration = 1.0f;
	Info.ExpireDuration = 4.0f;

	ActiveItem = FSlateNotificationManager::Get().AddNotification(Info);
	if (ActiveItem.IsValid())
	{
		ActiveItem->SetCompletionState(SNotificationItem::CS_Pending);
	}

	UMetaHumanPerformance* Performance = PerformanceWeak.Get();
	if (!Performance)
	{
		return;
	}

	CachedFrameRanges = Performance->GetPipelineFrameRangesArray();
	CachedFrameRangeIndex = Performance->GetPipelineFrameRange();

	CurrentPipelineStage = Performance->GetPipelineStage() + 1; // Starting stage
	TotalPipelineStages = Performance->GetTotalPipelineStage() * Performance->GetPipelineFrameRanges();

	if (ActiveItem.IsValid())
	{
		ActiveItem->SetText(FText::Format(LOCTEXT("StageStarted", "Processing stage {0} / {1}"), CurrentPipelineStage, TotalPipelineStages));
	}
}

void FMetaHumanPerformanceNotifier::FImpl::HandleFrameProcessed(int32 InFrameNumber)
{
	check(IsInGameThread());

	UpdateProgress(InFrameNumber);
}

void FMetaHumanPerformanceNotifier::FImpl::UpdateProgress(int32 InFrameNumber)
{
	if (!ActiveItem.IsValid())
	{
		return;
	}

	if (bShouldUpdateLocals)
	{
		UpdateLocalValues();

		ActiveItem->SetText(FText::Format(LOCTEXT("StageStarted", "Processing stage {0} / {1}"), CurrentPipelineStage, TotalPipelineStages));

		bShouldUpdateLocals = false;
	}

	if (!CachedFrameRanges.IsValidIndex(CachedFrameRangeIndex))
	{
		return;
	}

	const FFrameRange& CurrentFrameRange = CachedFrameRanges[CachedFrameRangeIndex];

	const double Now = FPlatformTime::Seconds();
	if ((Now - LastUpdateTime) < UpdateThrottleSeconds)
	{
		return;
	}

	LastUpdateTime = Now;

	FText SubText;
	SubText = FText::Format(LOCTEXT("ProgressFmt", "Processing frame {0} / {1}..."), 
							FText::AsNumber(InFrameNumber),
							FText::AsNumber(CurrentFrameRange.EndFrame - 1));

	ActiveItem->SetSubText(SubText);
}

void FMetaHumanPerformanceNotifier::FImpl::HandleStageFinished(int32 InStageCompleted)
{
	check(IsInGameThread());

	bShouldUpdateLocals = true;
	LastUpdateTime = 0.0;
}

void FMetaHumanPerformanceNotifier::FImpl::HandleProcessingFinished(
	TSharedPtr<const UE::MetaHuman::Pipeline::FPipelineData> InPipelineData)
{
	check(IsInGameThread());

	using UE::MetaHuman::Pipeline::EPipelineExitStatus;

	const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
	const FText ElapsedText = FText::AsNumber(FMath::RoundToInt(ElapsedSeconds));

	const EPipelineExitStatus Status = InPipelineData.IsValid() ? InPipelineData->GetExitStatus() : EPipelineExitStatus::Unknown;

	if (Status == EPipelineExitStatus::Ok)
	{
		FinishSuccess(FText::Format(LOCTEXT("DoneOk", "MetaHuman Performance processed in {0}s."), ElapsedText));
	}
	else if (Status == EPipelineExitStatus::Aborted)
	{
		FinishFailure(LOCTEXT("DoneAborted", "MetaHuman Performance processing was cancelled."));
	}
	else
	{
		FinishFailure(FText::Format(LOCTEXT("DoneFailedFmt", "MetaHuman Performance processing failed (status {0})."),
									FText::AsNumber(static_cast<int32>(Status))));
	}

	Unbind();
}

void FMetaHumanPerformanceNotifier::FImpl::FinishSuccess(const FText& InMessage)
{
	if (!ActiveItem.IsValid())
	{
		return;
	}

	ActiveItem->SetText(InMessage);
	ActiveItem->SetSubText(FText::GetEmpty());
	ActiveItem->SetCompletionState(SNotificationItem::CS_Success);
	ActiveItem->ExpireAndFadeout();
	ActiveItem.Reset();
}

void FMetaHumanPerformanceNotifier::FImpl::FinishFailure(const FText& InMessage)
{
	if (!ActiveItem.IsValid())
	{
		return;
	}

	ActiveItem->SetText(InMessage);

	ActiveItem->SetHyperlink(FSimpleDelegate::CreateLambda([]()
	{
		FOutputLogModule& OutputLogModule = FModuleManager::Get().LoadModuleChecked<FOutputLogModule>("OutputLog");
		OutputLogModule.FocusOutputLog();
	}), LOCTEXT("OpenOutputLog", "Open Output Log"));

	ActiveItem->SetCompletionState(SNotificationItem::CS_Fail);
	ActiveItem->ExpireAndFadeout();
	ActiveItem.Reset();
}

#undef LOCTEXT_NAMESPACE