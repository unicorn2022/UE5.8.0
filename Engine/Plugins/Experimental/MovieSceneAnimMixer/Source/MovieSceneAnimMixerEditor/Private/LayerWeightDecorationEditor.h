// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ISequencerDecorationEditor.h"
#include "ISequencerSection.h"

namespace UE::Sequencer
{

class FLayerWeightDecorationEditor : public ISequencerDecorationEditor
{
public:
	virtual UClass* GetDecorationClass() const override;

	virtual TSharedPtr<ISequencerSection> MakeSectionInterface(UMovieSceneSection& Section, UMovieSceneTrack& OwningTrack, FGuid ObjectBinding) override;
};

}
