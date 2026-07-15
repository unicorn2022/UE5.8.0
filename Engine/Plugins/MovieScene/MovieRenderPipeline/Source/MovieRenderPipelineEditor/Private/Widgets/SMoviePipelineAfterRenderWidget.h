// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ISinglePropertyView;

/**
 * Compact toolbar widget that shows the "After Render" behavior dropdown
 * and a settings button to open the post-render preferences.
 */
class SMoviePipelineAfterRenderWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMoviePipelineAfterRenderWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedPtr<ISinglePropertyView> PostRenderBehaviorWidget;
};
