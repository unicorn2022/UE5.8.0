// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Insights/ViewModels/TimingEventsTrack.h"

class FWorldStreamingTimingViewExtender;

class FWorldStreamingTrack : public FTimingEventsTrack
{
public:
	explicit FWorldStreamingTrack(FWorldStreamingTimingViewExtender& InExtender);
	virtual ~FWorldStreamingTrack() override = default;

	virtual void BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;

private:
	FWorldStreamingTimingViewExtender& Extender;
};