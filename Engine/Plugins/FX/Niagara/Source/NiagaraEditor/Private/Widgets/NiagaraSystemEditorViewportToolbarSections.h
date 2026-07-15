// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointerFwd.h"
#include "ToolMenuEntry.h"

class SNiagaraSystemViewport;

namespace UE::NiagaraSystemEditor
{
	extern FToolMenuEntry CreateShowSubmenu();
	extern void ExtendPreviewSceneSettingsSubmenu(FName InSubmenuName);
}
