// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "IChooserTableViewModel.h"

class FUICommandList;
class UChooserTable;

namespace UE::ChooserEditor
{

class IChooserTableView : public SCompoundWidget
{
public:
	virtual TSharedPtr<IChooserTableViewModel> GetViewModel() = 0;
};

}
