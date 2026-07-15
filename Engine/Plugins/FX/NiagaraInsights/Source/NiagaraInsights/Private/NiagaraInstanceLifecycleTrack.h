// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Insights/ViewModels/GraphTrack.h"

namespace UE::NiagaraInsights
{

class FNiagaraTimingViewSession;

class FNiagaraInstanceLifecycleTrack : public FGraphTrack
{
	using Super = FGraphTrack;

	INSIGHTS_DECLARE_RTTI(FNiagaraInstanceLifecycleTrack, FGraphTrack)

public:
	explicit FNiagaraInstanceLifecycleTrack(const FNiagaraTimingViewSession& InSharedData);

	// BEGIN: FGraphTrack interface
	virtual void Update(const ITimingTrackUpdateContext& Context) override;
	virtual void InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const override;
	// END: FGraphTrack interface

private:
	const FNiagaraTimingViewSession& SharedData;
};

} //namespace UE::NiagaraInsights
