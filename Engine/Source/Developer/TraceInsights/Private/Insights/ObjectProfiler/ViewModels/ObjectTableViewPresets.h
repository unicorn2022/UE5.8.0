// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Templates/SharedPointer.h"

namespace UE::Insights { class ITableTreeViewPreset; }

namespace UE::Insights::ObjectProfiler
{

class SObjectTableTreeView;

class FObjectTableViewPresets
{
public:
	static TSharedRef<ITableTreeViewPreset> CreateAssetViewPreset(SObjectTableTreeView& TableTreeView);
	static TSharedRef<ITableTreeViewPreset> CreateObjectViewPreset(SObjectTableTreeView& TableTreeView);
	static TSharedRef<ITableTreeViewPreset> CreateClassViewPreset(SObjectTableTreeView& TableTreeView);
	static TSharedRef<ITableTreeViewPreset> CreateOuterViewPreset(SObjectTableTreeView& TableTreeView);
};

} // namespace UE::Insights::ObjectProfiler
