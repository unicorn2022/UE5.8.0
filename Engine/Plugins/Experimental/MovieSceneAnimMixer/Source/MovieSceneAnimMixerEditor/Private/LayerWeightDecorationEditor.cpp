// Copyright Epic Games, Inc. All Rights Reserved.

#include "LayerWeightDecorationEditor.h"

#include "MovieSceneLayerWeightDecoration.h"

namespace UE::Sequencer
{
	UClass* FLayerWeightDecorationEditor::GetDecorationClass() const
	{
		return UMovieSceneLayerWeightDecoration::StaticClass();
	}

	TSharedPtr<ISequencerSection> FLayerWeightDecorationEditor::MakeSectionInterface(UMovieSceneSection& Section, UMovieSceneTrack& OwningTrack, FGuid ObjectBinding)
	{
		return MakeShared<FSequencerSection>(Section);
	}
}
