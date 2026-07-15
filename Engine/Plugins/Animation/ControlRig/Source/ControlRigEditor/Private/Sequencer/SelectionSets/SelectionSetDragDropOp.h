// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Misc/Guid.h"

class FAIESelectionSetDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FAIESelectionSetDragDropOp, FDecoratedDragDropOp)

	FGuid DraggedGuid;

	static TSharedRef<FAIESelectionSetDragDropOp> New(const FGuid& InGuid)
	{
		TSharedRef<FAIESelectionSetDragDropOp> Operation = MakeShared<FAIESelectionSetDragDropOp>();
		Operation->DraggedGuid = InGuid;
		Operation->DefaultHoverText = FText::GetEmpty();
		Operation->CurrentHoverText = FText::GetEmpty();
		Operation->Construct();
		return Operation;
	}
};
