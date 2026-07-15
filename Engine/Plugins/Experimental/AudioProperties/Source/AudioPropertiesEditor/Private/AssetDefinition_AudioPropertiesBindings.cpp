// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_AudioPropertiesBindings.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_AudioPropertiesBindings"

TConstArrayView<FAssetCategoryPath> UAssetDefinition_AudioPropertiesBindings::GetAssetCategories() const
{
	static const auto Categories = 
		{
			FAssetCategoryPath(EAssetCategoryPaths::Audio, 
			LOCTEXT("AssetAudioPropertiesBindingSubMenu", "Advanced"),
				FCategoryPath(LOCTEXT("AssetAudioPropertiesBindingSubMenuSection", "Properties"), ECategoryMenuType::Section))
		};
	return Categories;
}

#undef LOCTEXT_NAMESPACE
