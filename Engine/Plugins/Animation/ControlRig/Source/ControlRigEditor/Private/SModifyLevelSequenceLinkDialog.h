// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "LevelSequenceAnimSequenceLink.h"

DECLARE_DELEGATE_OneParam(FModifyLevelSequenceLinkDelegate, const FLevelSequenceAnimSequenceLinkItem&);

struct ModifyLevelSequenceLinkDialog
{
	static void ShowDialog(const FLevelSequenceAnimSequenceLinkItem& InLinkItem, FModifyLevelSequenceLinkDelegate InDelegate);
};
