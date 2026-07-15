// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVM/Views/KeyRenderer.h"
#include "SequencerSelectedKey.h"

class FSequencer;
enum class ESelectionPreviewState;

namespace UE::Sequencer::ToolableTimeline
{

class FToolableTimelineKeySelection;

struct FToolableTimelineKeyRenderer : IKeyRendererInterface
{
	FToolableTimelineKeyRenderer(const FToolableTimelineKeySelection& InKeySelection);

	//~ Begin IKeyRendererInterface

	virtual bool HasAnySelectedKeys() const override;
	virtual bool HasAnyPreviewSelectedKeys() const override;
	virtual bool HasAnyHoveredKeys() const override;

	virtual bool IsKeySelected(const TViewModelPtr<IKeyExtension>& InOwner, const FKeyHandle InKey) const override;
	virtual bool IsKeyHovered(const TViewModelPtr<IKeyExtension>& InOwner, const FKeyHandle InKey) const override;

	virtual EKeySelectionPreviewState GetPreviewSelectionState(const TViewModelPtr<IKeyExtension>& InOwner, const FKeyHandle InKey) const override;

	//~ End IKeyRendererInterface

private:
	TSet<FSequencerSelectedKey> HoveredKeys;

	TMap<FSequencerSelectedKey, ESelectionPreviewState> SelectionPreview;

	TSet<FSequencerSelectedKey> SelectedKeys;
};

} // namespace UE::Sequencer::ToolableTimeline
