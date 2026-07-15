// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanDefaultEditorPipelineLog.h"
#include "MetaHumanDefaultEditorPipelineBase.h"

#include "Customizations/MetaHumanMaterialBakingPropertiesDetailCustomization.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

class FMetaHumanDefaultEditorPipelineModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
		MetaHumanMaterialBakingPropertiesName = FMetaHumanMaterialBakingProperties::StaticStruct()->GetFName();

		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

		PropertyModule.RegisterCustomPropertyTypeLayout(MetaHumanMaterialBakingPropertiesName,
														FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FMetaHumanMaterialBakingPropertiesDetailCustomization::MakeInstance));
	}

	virtual void ShutdownModule() override
	{
		if (FPropertyEditorModule* PropertyModulePtr = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
		{
			PropertyModulePtr->UnregisterCustomPropertyTypeLayout(MetaHumanMaterialBakingPropertiesName);
		}
	}

private:
	FName MetaHumanMaterialBakingPropertiesName;
};

IMPLEMENT_MODULE(FMetaHumanDefaultEditorPipelineModule, MetaHumanDefaultEditorPipeline);

DEFINE_LOG_CATEGORY(LogMetaHumanDefaultEditorPipeline);
