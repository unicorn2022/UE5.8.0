// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ColorGradingMixerObjectFilterRegistry.h"

/** Color grading mixer hierarchy config for displaying UCompositePassColorGrading objects in the color grading drawer */
struct FColorGradingHierarchyConfig_Composite : IColorGradingMixerObjectHierarchyConfig
{
public:
	static TSharedRef<IColorGradingMixerObjectHierarchyConfig> MakeInstance() { return MakeShared<FColorGradingHierarchyConfig_Composite>(); }

	//~ IColorGradingMixerObjectHierarchyConfig interface
	virtual TArray<FObjectMixerEditorActorSubObject> GetActorSubObjects(UObject* ParentObject) const override;
	virtual TSet<FName> GetPropertiesThatRequireListRefresh() const override;
	//~ End IColorGradingMixerObjectHierarchyConfig interface
};
