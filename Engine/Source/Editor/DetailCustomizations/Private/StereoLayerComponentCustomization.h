// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "UObject/WeakObjectPtr.h"

class UStereoLayerComponent;
class IDetailPropertyRow;

class FStereoLayerComponentCustomization : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	
private:
	[[nodiscard]] bool ShouldEnableDepthSupport() const;
	
	TWeakObjectPtr<const UStereoLayerComponent> WeakComponent;
};
