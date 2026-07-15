// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterPaletteEditorModule.h"

#include "Customizations/MetaHumanCharacterPaletteItemWrapperCustomization.h"
#include "Customizations/CollectionInstanceParametersCustomization.h"
#include "MetaHumanCharacterPaletteEditorLog.h"
#include "MetaHumanCharacterPaletteItemWrapper.h"
#include "PaletteEditor/MetaHumanCharacterPaletteEditorToolkit.h"
#include "MetaHumanSDKEditor.h"
#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"
#include "PaletteEditor/MetaHumanCharacterPaletteEditorCommands.h"
#include "PropertyEditorModule.h"

DEFINE_LOG_CATEGORY(LogMetaHumanCharacterPaletteEditor);

#define LOCTEXT_NAMESPACE "MetaHumanCharacterPaletteEditor"

FMetaHumanCharacterPaletteEditorModule& FMetaHumanCharacterPaletteEditorModule::GetChecked()
{
	return FModuleManager::GetModuleChecked<FMetaHumanCharacterPaletteEditorModule>(UE_MODULE_NAME);
}

void FMetaHumanCharacterPaletteEditorModule::StartupModule()
{
	FMetaHumanCharacterPaletteEditorCommands::Register();

	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	MessageLogModule.RegisterLogListing(UE::MetaHuman::MessageLogName, LOCTEXT("MessageLogChannelName", "MetaHuman"));

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyEditorModule.RegisterCustomClassLayout(
		UMetaHumanCharacterPaletteItemWrapper::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FMetaHumanCharacterPaletteItemWrapperCustomization::MakeInstance));

	PropertyEditorModule.RegisterCustomClassLayout(
		UMetaHumanPaletteEditorCollectionInstanceParameters::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FCollectionInstanceParametersCustomization::MakeInstance));
}

void FMetaHumanCharacterPaletteEditorModule::ShutdownModule()
{
	FMetaHumanCharacterPaletteEditorCommands::Unregister();

	if (FPropertyEditorModule* PropertyEditorModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		PropertyEditorModule->UnregisterCustomClassLayout(
			UMetaHumanCharacterPaletteItemWrapper::StaticClass()->GetFName());

		PropertyEditorModule->UnregisterCustomClassLayout(
			UMetaHumanPaletteEditorCollectionInstanceParameters::StaticClass()->GetFName());
	}
}

IMPLEMENT_MODULE(FMetaHumanCharacterPaletteEditorModule, MetaHumanCharacterPaletteEditor);

#undef LOCTEXT_NAMESPACE
