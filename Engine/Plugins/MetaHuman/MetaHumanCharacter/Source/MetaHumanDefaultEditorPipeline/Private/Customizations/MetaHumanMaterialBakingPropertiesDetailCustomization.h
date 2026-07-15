// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"

/**
 * Customization for the FMetaHumanMaterialBakingProperties struct.
 * Displays a segmented control to switch between Face and Body baking options,
 * then shows output textures grouped by category with resolution selectors.
 */
class FMetaHumanMaterialBakingPropertiesDetailCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	//~Begin IPropertyTypeCustomization interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& InCustomizationUtils) override;
	//~End IPropertyTypeCustomization interface

private:

	enum class EBakingTab : uint8
	{
		Head,
		Body
	};

	void AddBakingOptionsWidgets(
		IDetailChildrenBuilder& InChildBuilder,
		TSharedRef<IPropertyHandle> BakingOptionsHandle,
		TSharedPtr<EBakingTab> InActiveTab,
		EBakingTab Tab);

	TSharedPtr<EBakingTab> ActiveTab;
};
