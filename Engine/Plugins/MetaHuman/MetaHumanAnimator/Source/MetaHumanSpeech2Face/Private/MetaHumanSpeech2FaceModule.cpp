// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanSpeech2FaceModule.h"

#include "AudioDrivenAnimationConfig.h"
#include "AudioDrivenAnimationCustomizations.h"

#if WITH_EDITOR
#include "PropertyEditorModule.h"
#endif // WITH_EDTIOR

void FMetaHumanSpeech2FaceModule::StartupModule()
{
#if WITH_EDITOR
	using namespace UE::MetaHuman::Private;

	AudioDrivenAnimationSolveOverridesName = FAudioDrivenAnimationSolveOverrides::StaticStruct()->GetFName();

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout(
		AudioDrivenAnimationSolveOverridesName,
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FAudioSolveOverridesPropertyTypeCustomization::MakeInstance)
	);

	PropertyModule.NotifyCustomizationModuleChanged();
#endif // WITH_EDITOR
}

void FMetaHumanSpeech2FaceModule::ShutdownModule()
{
#if WITH_EDITOR
	if (UObjectInitialized())
	{
		FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");

		if (PropertyModule)
		{
			PropertyModule->UnregisterCustomPropertyTypeLayout(AudioDrivenAnimationSolveOverridesName);
			PropertyModule->NotifyCustomizationModuleChanged();
		}
	}
#endif // WITH_EDITOR
}

IMPLEMENT_MODULE(FMetaHumanSpeech2FaceModule, MetaHumanSpeech2Face)
