// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FExtender;
struct FInsightsMajorTabConfig;

namespace UE::Insights::ObjectProfiler
{

/** Ribbon based toolbar used as a main menu in the Profiler window. */
class SObjectProfilerToolbar : public SCompoundWidget
{
public:
	/** Default constructor. */
	SObjectProfilerToolbar();

	/** Virtual destructor. */
	virtual ~SObjectProfilerToolbar();

	SLATE_BEGIN_ARGS(SObjectProfilerToolbar) {}
	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 *
	 * @param InArgs The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs, const FInsightsMajorTabConfig& Config);
};

} // namespace UE::Insights::ObjectProfiler
