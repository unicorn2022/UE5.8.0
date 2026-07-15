// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMovieSceneAnimMixerBakeProvider.h"

/**
 * Concrete implementation of IMovieSceneAnimMixerBakeProvider.
 * Delegates to UE::ControlRig::BuildBakeToControlRigMenu() and manages
 * FControlRigEditMode activation after a successful bake.
 */
class FMixedControlRigBakeProvider : public IMovieSceneAnimMixerBakeProvider
{
public:
	virtual void BuildBakeToControlRigMenuSection(
		FMenuBuilder& MenuBuilder,
		const FAnimMixerBakeMenuParams& Params) override;
};
