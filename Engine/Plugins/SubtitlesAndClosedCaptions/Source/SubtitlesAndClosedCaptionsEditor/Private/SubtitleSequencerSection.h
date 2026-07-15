// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ISequencerSection.h"

class FSequencerSectionPainter;

class FSubtitleSequencerSection
	: public FSequencerSection
	, public TSharedFromThis<FSubtitleSequencerSection>
{
public:
	FSubtitleSequencerSection(UMovieSceneSection& InSection)
		: FSequencerSection(InSection)
	{}

	// ISequencerSection interface
	virtual FText GetSectionTitle() const override;
	virtual int32 OnPaintSection(FSequencerSectionPainter& Painter) const override;
};
