// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Templates/SharedPointer.h"

namespace UE::SandboxedEditing
{
class FPersistOperationViewModel;
class IFileStateColumnBehavior;
class IFileStateColumnWidgetFactory;

struct FFileStateColumnRegistry
{
	/** The behavior of the columns for view models. */
	TMap<FName, TSharedRef<IFileStateColumnBehavior>> ColumnBehaviors;
	
	/** Creates the widgets for the columns. */
	TMap<FName, TSharedRef<IFileStateColumnWidgetFactory>> ColumnFactories;
	
	TArray<TSharedRef<IFileStateColumnBehavior>> ToBehaviorArray() const
	{
		TArray<TSharedRef<IFileStateColumnBehavior>> Behaviors;
		ColumnBehaviors.GenerateValueArray(Behaviors);
		return Behaviors;
	}
};
	
/** @return Columns to show in the browser view. */
FFileStateColumnRegistry GetColumnsForBrowser();

/** @return Columns to show in the active sandbox view. */
FFileStateColumnRegistry GetColumnsForActiveSandbox();

/** @return Columns to show in the persist view. */
FFileStateColumnRegistry GetColumnsForPersist(const TSharedRef<FPersistOperationViewModel>& InViewModel);
}