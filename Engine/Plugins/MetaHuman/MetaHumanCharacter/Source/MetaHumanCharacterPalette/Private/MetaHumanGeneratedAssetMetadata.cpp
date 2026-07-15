// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanGeneratedAssetMetadata.h"

FMetaHumanGeneratedAssetMetadata::FMetaHumanGeneratedAssetMetadata(TObjectPtr<UObject> InObject, const FString& InPreferredSubfolderPath, const FString& InPreferredName, bool bInSubfolderIsAbsolute)
	: Object(InObject)
	, PreferredSubfolderPath(InPreferredSubfolderPath)
	, bSubfolderIsAbsolute(bInSubfolderIsAbsolute)
	, PreferredName(InPreferredName)
{
}
