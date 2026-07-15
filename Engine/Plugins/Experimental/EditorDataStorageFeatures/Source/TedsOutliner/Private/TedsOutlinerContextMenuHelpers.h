// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UObject/WeakObjectPtrTemplates.h"

class UToolMenu;
class UWorld;

namespace UE::Editor::Outliner::Helpers
{
	// Snapshot the current outliner selection and dispatch to all registered section builders.
	// Call this from ModifyContextMenu / AddDynamicSection in GetSceneOutlinerInitializationOptions.
	void BuildTedsOutlinerContextMenu(UToolMenu* InMenu, TWeakObjectPtr<UWorld> World);
}
