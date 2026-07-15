// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Channels/DoubleChannelCurveModel.h"

struct FMovieSceneTimeWarpChannel;

class FTimeWarpChannelCurveModel : public FDoubleChannelCurveModel
{
public:

	FTimeWarpChannelCurveModel(TMovieSceneChannelHandle<FMovieSceneTimeWarpChannel> InChannel, UMovieSceneSection* InOwningSection, UObject* InOwningObject, TWeakPtr<ISequencer> InWeakSequencer);

	void SetCurveAttributes(const FCurveAttributes& InCurveAttributes) override;

	void GetCurveAttributes(FCurveAttributes& OutCurveAttributes) const override;

	void AllocateAxes(FCurveEditor* InCurveEditor, TSharedPtr<FCurveEditorAxis>& OutHorizontalAxis, TSharedPtr<FCurveEditorAxis>& OutVerticalAxis) const override;

	void MakeChildCurves(TArray<TUniquePtr<FCurveModel>>& OutChildCurves) const override;

	// Time warp getters are parented to UMovieSceneSequence (not a UMovieSceneSection), so the
	// default outer-chain walk fails to find the section. When a caller asks for a section (or
	// any of its bases) we return our directly-stored section instead.
	virtual UObject* GetOwningObjectOrOuter(UClass* Class) const override;
};