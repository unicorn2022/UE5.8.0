// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SCapturedActorsWidget.h"
#include "Widgets/Views/SListView.h"

class ITableRow;
class STableViewBase;
struct FCapturableProperty;

class SCapturedActorsWidgetTwo : public SCapturedActorsWidget
{
public:
	SLATE_BEGIN_ARGS(SCapturedActorsWidgetTwo) { }
		SLATE_ARGUMENT(const TArray<UObject*>*, Actors)
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);
	
private:
	TSharedRef<ITableRow> MakeRow(UObject* Item, const TSharedRef<STableViewBase>& OwnerTable) const;
	void OnSelectionChanged(UObject* Object, ESelectInfo::Type SelectType);

	TSharedPtr<SListView<UObject*>> ActorListView;
};
