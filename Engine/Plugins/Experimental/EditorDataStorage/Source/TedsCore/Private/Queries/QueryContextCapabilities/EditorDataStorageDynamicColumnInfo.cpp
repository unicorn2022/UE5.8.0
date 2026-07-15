// Copyright Epic Games, Inc. All Rights Reserved.

#include "Queries/QueryContextCapabilities/EditorDataStorageDynamicColumnInfo.h"

#include "Queries/QueryContextCapabilities/ContextEnvironment.h"

namespace UE::Editor::DataStorage::Queries
{
	const UScriptStruct* FDynamicColumnInfoContextCapability::FindDynamicColumnType(
		const IContextEnvironment& Environment, const UScriptStruct& TemplateType, const FName& Identifier)
	{
		return Environment.GetEnvironment()->FindDynamicColumn(TemplateType, Identifier);
	}

	void FDynamicColumnInfoContextCapability::ForEachDynamicColumnType(
		const IContextEnvironment& Environment, const UScriptStruct& TemplateType, TFunctionRef<void(const UScriptStruct& Type)> Callback)
	{
		Environment.GetEnvironment()->ForEachDynamicColumn(TemplateType, Callback);
	}
} // namespace UE::Editor::DataStorage::Queries
