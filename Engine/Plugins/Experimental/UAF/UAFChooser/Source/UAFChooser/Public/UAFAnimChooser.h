// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chooser.h"
#include "UAFAnimChooser.generated.h"

/**
* UAF specific subclass of chooser table, for selecting assets which can generate anim graphs
*/
UCLASS()
class UUAFAnimChooserTable : public UChooserTable
{
	GENERATED_UCLASS_BODY()
public:
	UUAFAnimChooserTable() {}

	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;
};
