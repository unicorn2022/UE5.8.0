// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraUISpriteRendererProperties.h"
#include "NiagaraEditorModule.h"
#include "Modules/ModuleManager.h"

class FNiagaraUIRendererEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FNiagaraEditorModule& NiagaraEditor = FModuleManager::LoadModuleChecked<FNiagaraEditorModule>("NiagaraEditor");

		NiagaraEditor.RegisterRendererCreationInfo(FNiagaraRendererCreationInfo(
			UNiagaraUISpriteRendererProperties::StaticClass()->GetDisplayNameText(),
			FText::FromString(UNiagaraUISpriteRendererProperties::StaticClass()->GetDescription()),
			UNiagaraUISpriteRendererProperties::StaticClass()->GetClassPathName(),
			FNiagaraRendererCreationInfo::FRendererFactory::CreateLambda([](UObject* OuterEmitter)
			{
				return NewObject<UNiagaraUISpriteRendererProperties>(OuterEmitter, NAME_None, RF_Transactional);
			})));
	}

	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FNiagaraUIRendererEditorModule, NiagaraUIRendererEditor)
