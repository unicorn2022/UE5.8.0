// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterAssemblySettings.h"

FMetaHumanCharacterAssemblySettings::FMetaHumanCharacterAssemblySettings()
{
	if (IModularFeatures::Get().IsModularFeatureAvailable(IMetaHumanCharacterAssemblySettingsExtender::FeatureName))
	{
		TArray<IMetaHumanCharacterAssemblySettingsExtender*> Extenders = IModularFeatures::Get().GetModularFeatureImplementations<IMetaHumanCharacterAssemblySettingsExtender>(IMetaHumanCharacterAssemblySettingsExtender::FeatureName);
		for (IMetaHumanCharacterAssemblySettingsExtender* Extender : Extenders)
		{
			Extender->PostConstructionEdit(this);
		}
	}
}

FMetaHumanCharacterAssemblySettings::~FMetaHumanCharacterAssemblySettings() = default;