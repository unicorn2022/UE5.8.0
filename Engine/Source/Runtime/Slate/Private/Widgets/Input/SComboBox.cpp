// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Input/SComboBox.h"

#include "HAL/IConsoleManager.h"

namespace SlateComboRow
{
	bool bMouseButtonHandlingV2 = true;
}

FAutoConsoleVariableRef CVarComboRowMouseButtonDownHandlingV2(
	TEXT("Slate.ComboRowMouseButtonHandlingV2"),
	SlateComboRow::bMouseButtonHandlingV2,
	TEXT("Whether to enable new mouse button event handling on SComboRow")
);
