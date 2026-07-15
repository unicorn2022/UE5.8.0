// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleInterface.h"

class SDockTab;
class SShaderAuditWidget;

class FShaderAuditEditorModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedRef<SDockTab> SpawnToolTab(const FSpawnTabArgs& Args);

	TSharedPtr<SShaderAuditWidget> AuditWidget;
};
