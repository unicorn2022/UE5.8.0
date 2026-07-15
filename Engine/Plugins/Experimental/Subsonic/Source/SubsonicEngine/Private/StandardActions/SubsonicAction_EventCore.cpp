// Copyright Epic Games, Inc. All Rights Reserved.

#include "StandardActions/SubsonicAction_EventCore.h"

#include "SubsonicExecutor.h"


namespace UE::Subsonic
{
	void FSubsonicEventAction_DelayEvent::Execute(const Core::FSubsonicExecutor& InExecutor, const Core::FActionHandle& InHandle) const
	{
		if (EventName.IsValid())
		{
			if (Delay > 0.0f)
			{
				InExecutor.ExecuteEvent(EventName.GetTagName());
			}
			else
			{
				// TODO: Implement delay instance state tracking in subscriber & cycle detection
				// Detection likely should be implemented in validation step required to execute
				// in non-runtime contexts for perf reasons.
				InExecutor.ExecuteEvent(EventName.GetTagName());
			}
		}
	}
} // namespace UE::Subsonic