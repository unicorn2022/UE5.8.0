// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CompositeLayerCustomization.h"

class IDetailLayoutBuilder;

/**
 * Customization for the composite single light shadow layer, for displaying custom widgets for picking actors and managing passes
 */
class FCompositeLayerSingleLightShadowCustomization : public FCompositeLayerCustomization
{
public:
	/** Makes a new instance of the details customization */
	static TSharedRef<IDetailCustomization> MakeInstance();

	//~ Begin FCompositeLayerCustomization
	virtual void CustomizeLayerDetails(IDetailLayoutBuilder& InDetailLayout) override;
	//~ End FCompositeLayerCustomization
};
