// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToolableTimelineChannelCache.h"
#include "ISequencerTrackEditor.h"
#include "MVVM/Selection/Selection.h"
#include "MVVM/ViewModels/ChannelModel.h"
#include "Sequencer.h"
#include "SimpleView/SequencerSimpleViewExtender.h"
#include "ToolableTimeline/ToolableTimeline.h"

namespace UE::Sequencer::ToolableTimeline
{

static void GatherChannelsFromViewModel(const TViewModelPtr<FViewModel>& InViewModel
	, TSet<TWeakViewModelPtr<FChannelModel>>& OutWeakChannelModels)
{
	if (!InViewModel.IsValid())
	{
		return;
	}

	if (FChannelModel* const ChannelModel = InViewModel->CastThis<FChannelModel>())
	{
		if (ChannelModel->GetKeyArea().IsValid())
		{
			OutWeakChannelModels.Add(InViewModel->CastThisShared<FChannelModel>());
		}
	}

	if (FChannelGroupModel* const ChannelGroup = InViewModel->CastThis<FChannelGroupModel>())
	{
		for (const TWeakViewModelPtr<FChannelModel>& WeakChannel : ChannelGroup->GetChannels())
		{
			const TViewModelPtr<FChannelModel> Channel = WeakChannel.Pin();
			if (Channel.IsValid() && Channel->GetKeyArea().IsValid())
			{
				OutWeakChannelModels.Add(WeakChannel);
			}
		}
	}

	for (FViewModelListIterator ChildIt = InViewModel->GetChildren(EViewModelListType::Outliner); ChildIt; ++ChildIt)
	{
		if (const TViewModelPtr<IOutlinerExtension> ChildOutlinerItem = ChildIt->CastThisShared<IOutlinerExtension>())
		{
			GatherChannelsFromViewModel(ChildOutlinerItem, OutWeakChannelModels);
		}
	}
}

static void GatherChannelsFromPinnedOutlinerItem(const TViewModelPtr<FViewModel>& InViewModel
	, TSet<TWeakViewModelPtr<FChannelModel>>& OutWeakChannelModels)
{
	if (!InViewModel.IsValid())
	{
		return;
	}

	if (const IPinnableExtension* const PinnableExtension = InViewModel->CastThis<IPinnableExtension>())
	{
		if (PinnableExtension && PinnableExtension->IsPinned())
		{
			GatherChannelsFromViewModel(InViewModel, OutWeakChannelModels);
			return;
		}
	}

	for (FViewModelListIterator ChildIt = InViewModel->GetChildren(EViewModelListType::Outliner); ChildIt; ++ChildIt)
	{
		GatherChannelsFromPinnedOutlinerItem(*ChildIt, OutWeakChannelModels);
	}
}

static TSet<TWeakViewModelPtr<FChannelModel>> GetAllChannels(const ISequencer& InSequencer)
{
	TSet<TWeakViewModelPtr<FChannelModel>> OutWeakChannelModels;

	const TSharedPtr<FSequencerEditorViewModel> RootViewModel = InSequencer.GetViewModel();
	if (!RootViewModel.IsValid())
	{
		return OutWeakChannelModels;
	}

	const TViewModelPtr<FSequenceModel> RootSequenceModel = RootViewModel->GetRootSequenceModel();
	if (!RootSequenceModel.IsValid())
	{
		return OutWeakChannelModels;
	}

	GatherChannelsFromViewModel(RootSequenceModel, OutWeakChannelModels);

	return OutWeakChannelModels;
}

static TSet<TWeakViewModelPtr<FChannelModel>> GetChannelsFromSelectedOutlinerItems(const ISequencer& InSequencer)
{
	TSet<TWeakViewModelPtr<FChannelModel>> OutWeakChannelModels;

	const TSharedPtr<FSequencerEditorViewModel> RootViewModel = InSequencer.GetViewModel();
	if (!RootViewModel.IsValid())
	{
		return OutWeakChannelModels;
	}

	const TSharedPtr<FSequencerSelection> Selection = RootViewModel->GetSelection();
	if (!Selection.IsValid())
	{
		return OutWeakChannelModels;
	}

	for (const TViewModelPtr<IOutlinerExtension>& SelectedOutlinerItem : Selection->Outliner)
	{
		GatherChannelsFromViewModel(SelectedOutlinerItem, OutWeakChannelModels);
	}

	return OutWeakChannelModels;
}

static TSet<TWeakViewModelPtr<FChannelModel>> GetChannelsFromPinnedOutlinerItems(const ISequencer& InSequencer)
{
	TSet<TWeakViewModelPtr<FChannelModel>> OutWeakChannelModels;

	const TSharedPtr<FSequencerEditorViewModel> RootViewModel = InSequencer.GetViewModel();
	if (!RootViewModel.IsValid())
	{
		return OutWeakChannelModels;
	}

	const TViewModelPtr<FSequenceModel> RootSequenceModel = RootViewModel->GetRootSequenceModel();
	if (!RootSequenceModel.IsValid())
	{
		return OutWeakChannelModels;
	}

	GatherChannelsFromPinnedOutlinerItem(RootSequenceModel, OutWeakChannelModels);

	return OutWeakChannelModels;
}

FToolableTimelineChannelCache::FToolableTimelineChannelCache(const FToolableTimeline& InTimeline)
{
	const TSharedPtr<FSequencer> Sequencer = InTimeline.GetSequencer();
	if (!Sequencer.IsValid())
	{
		return;
	}

	const FSequencer& SequencerRef = *Sequencer;

	switch (InTimeline.GetTimelineSettings().Settings.KeyDisplay)
	{
	default:
	case ETimelineKeyDisplay::SelectedAndPinned:
		WeakChannelModels.Append(GetChannelsFromSelectedOutlinerItems(SequencerRef));
		GatherChannelsFromAdditionalSelectedViewModels(SequencerRef, WeakChannelModels);
		WeakChannelModels.Append(GetChannelsFromPinnedOutlinerItems(SequencerRef));
		break;

	case ETimelineKeyDisplay::Selected:
		WeakChannelModels.Append(GetChannelsFromSelectedOutlinerItems(SequencerRef));
		GatherChannelsFromAdditionalSelectedViewModels(SequencerRef, WeakChannelModels);
		break;

	case ETimelineKeyDisplay::All:
		WeakChannelModels.Append(GetAllChannels(SequencerRef));
		break;
	}

	ApplyExtendedFilters(InTimeline);
}

const TSet<TWeakViewModelPtr<FChannelModel>>& FToolableTimelineChannelCache::GetChannelModels() const
{
	return WeakChannelModels;
}

void FToolableTimelineChannelCache::ApplyExtendedFilters(const FToolableTimeline& InTimeline)
{
	const TSharedPtr<FSequencer> Sequencer = InTimeline.GetSequencer();
	if (!Sequencer.IsValid())
	{
		return;
	}

	if (InTimeline.GetChannelFilters().IsEmpty())
	{
		return;
	}

	for (auto It = WeakChannelModels.CreateIterator(); It; ++It)
	{
		const TViewModelPtr<FChannelModel> ChannelModel = It->Pin();
		if (!ChannelModel.IsValid())
		{
			It.RemoveCurrent();
			continue;
		}

		if (!InTimeline.PassesChannelFilters(*ChannelModel))
		{
			It.RemoveCurrent();
		}
	}
}

void FToolableTimelineChannelCache::GatherChannelsFromAdditionalSelectedViewModels(const ISequencer& InSequencer
	, TSet<TWeakViewModelPtr<FChannelModel>>& OutWeakChannelModels)
{
	for (const TPair<FDelegateHandle, FOnEnumerateAdditionalSimpleViewSelectedModels::FDelegate>& AdditionalModels
		: SimpleView::FSequencerSimpleViewExtender::GetRegisteredAdditionalSelectedModels())
	{
		AdditionalModels.Value.ExecuteIfBound(InSequencer, [&OutWeakChannelModels](const FViewModelPtr& InViewModel)
			{
				GatherChannelsFromViewModel(InViewModel, OutWeakChannelModels);
				return true;
			});
	}
}

} // namespace UE::Sequencer::ToolableTimeline
