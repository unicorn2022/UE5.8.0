// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorFolderElementLevelEditorCommonActionsCustomization.h"

#include "HAL/IConsoleManager.h"

bool FActorFolderElementLevelEditorCommonActionsCustomization::IsCopyCapable() const
{
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("TEDS.Feature.Folders.TypedElements"));
	
	return CVar && CVar->GetBool();
}
