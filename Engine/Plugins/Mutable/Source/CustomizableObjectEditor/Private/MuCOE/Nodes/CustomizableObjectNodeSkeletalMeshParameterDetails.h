// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CustomizableObjectNodeParameterDetails.h"

#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
struct FLayoutEditorMeshSection;


class FCustomizableObjectNodeSkeletalMeshParameterDetails : public FCustomizableObjectNodeParameterDetails
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** ILayoutDetails interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:

	void GenerateMeshSectionOptions(TArray<FLayoutEditorMeshSection>& OutMeshSections);

	// Pointer to the node represented in this details
	class UCustomizableObjectNodeSkeletalMeshParameter* Node;
};
