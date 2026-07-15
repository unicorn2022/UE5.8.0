// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

struct FSlateBrush;
class SBorder;

/** Widget used as an overlay for the 2D viewport in MetaHuman Character Editor. */
class SMetaHumanCharacterEditor2DViewportOverlay : public SCompoundWidget
{

public:
	SLATE_BEGIN_ARGS(SMetaHumanCharacterEditor2DViewportOverlay)
		{}

		/** The text to be displayed as the label for the overlay. */
		SLATE_ATTRIBUTE(FText, Label)

		/** The image brush to be used as the overlay. */
		SLATE_ATTRIBUTE(const FSlateBrush*, ImageBrush)

		/** Invoked when the mouse is pressed in the widget. */
		SLATE_EVENT(FPointerEventHandler, OnMouseButtonDown)

	SLATE_END_ARGS()

	/** Constructs the widget. */
	void Construct(const FArguments& InArgs);

private:

	/** Gets the border image brush used for the overlay. */
	const FSlateBrush* GetOverlayBorderImageBrush() const;

	/** The border widget used to display the overlay image. */
	TSharedPtr<SBorder> OverlayBorder;
};
