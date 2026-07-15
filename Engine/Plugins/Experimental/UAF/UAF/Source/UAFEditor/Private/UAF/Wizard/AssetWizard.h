// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UUAFTemplateDataAsset;
class UUAFTemplateConfig;
class UWorkspace;

namespace UE::UAF::Editor
{
	class FAssetWizard
	{
	public:
		static void Launch();
		
	private:
		static void ShowTemplatePicker();

		static void CreateAssets(const TObjectPtr<const UUAFTemplateDataAsset> Template, const TObjectPtr<const UUAFTemplateConfig> TemplateConfig);
	};
}