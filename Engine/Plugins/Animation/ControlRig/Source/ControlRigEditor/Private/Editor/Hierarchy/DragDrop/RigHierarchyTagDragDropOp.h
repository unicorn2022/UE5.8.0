// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DragAndDrop/DecoratedDragDropOp.h"

class SRigHierarchyTagWidget;

class FRigHierarchyTagDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FRigHierarchyTagDragDropOp, FDecoratedDragDropOp)

	static TSharedRef<FRigHierarchyTagDragDropOp> New(TSharedPtr<SRigHierarchyTagWidget> InTagWidget);

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;

	/** @return The identifier being dragged */
	const FString& GetIdentifier() const
	{
		return Identifier;
	}

private:

	FText Text;
	FString Identifier;
};
