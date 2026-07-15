// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modification/Resolution/CurveModelLookUpInfo.h"
#include "Templates/UniquePtr.h"

class FCurveModel;
class FSequencer;
namespace UE::CurveEditor { struct FCurveModelLookUpArgs; }

namespace UE::Sequencer
{
/** Constructs a FCurveModel by looking up the channel from the owning UMovieSceneSection and ChannelName. */
TUniquePtr<FCurveModel> ResolveCurveEditorModel(const CurveEditor::FCurveModelLookUpArgs& InArgs, TWeakPtr<FSequencer> OwningSequencer);
} // namespace UE::Sequencer
