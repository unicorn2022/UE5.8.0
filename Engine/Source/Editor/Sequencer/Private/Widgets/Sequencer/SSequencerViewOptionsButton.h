// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/SlateDelegates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

namespace UE::Sequencer
{
/** Implements view options for Sequencer. */
class SSequencerViewOptionsButton : public SCompoundWidget
{
public:
	
	SLATE_BEGIN_ARGS(SSequencerViewOptionsButton){}
		/** Gets the content of the view options. */
		SLATE_EVENT(FOnGetContent, OnGetMenuContent)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
} // namespace UE::Sequencer

