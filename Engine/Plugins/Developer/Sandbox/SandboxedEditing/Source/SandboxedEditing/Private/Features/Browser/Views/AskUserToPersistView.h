// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

namespace UE::SandboxedEditing
{
class FAskUserToPersistWorkflow;
class FLeaveSandboxViewModel;

/** Visualizes FAskUserToPersistWorkflow by displaying a dialogue. */
class FAskUserToPersistView
{
public:
	
	explicit FAskUserToPersistView(const TSharedRef<FLeaveSandboxViewModel>& InLeaveViewModel);
	~FAskUserToPersistView();
	
private:
	
	/** Notifies when FAskUserToPersistWorkflow starts.  */
	const TSharedRef<FLeaveSandboxViewModel> LeaveViewModel;

	void ShowNotification(FAskUserToPersistWorkflow& InWorkflow) const;
};
}
