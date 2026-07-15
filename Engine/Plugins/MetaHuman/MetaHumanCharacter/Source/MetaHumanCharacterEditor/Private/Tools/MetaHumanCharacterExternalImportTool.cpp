// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/MetaHumanCharacterExternalImportTool.h"
#include "Tools/MetaHumanCharacterEditorToolTargetUtil.h"

#define LOCTEXT_NAMESPACE "MetaHumanExternalImportTool"

FText UMetaHumanCharacterExternalImportTool::GetWarningText() const
{
	return FText::GetEmpty();
}

UMetaHumanCharacter* UMetaHumanCharacterExternalImportTool::GetTargetMetaHumanCharacter() const
{
	return UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
}

#undef LOCTEXT_NAMESPACE
