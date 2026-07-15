// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Internationalization/Text.h"
#include "Math/Color.h"
#include "Templates/SharedPointer.h"

namespace UE::Insights
{

class IBarGraphSegment
{
public:
	virtual ~IBarGraphSegment() = default;

	/**
	 * Returns the relative segment size, in interval [0 .. 1].
	 */
	virtual double GetSize() const = 0;

	/**
	 * Returns the text displayed on the segment.
	 */
	virtual FText GetText() const = 0;

	/**
	 * Returns the tool tip text of the segment.
	 */
	virtual FText GetToolTipText() const = 0;

	/**
	 * Returns the color of the segment.
	 */
	virtual FLinearColor GetColor() const = 0;

	/**
	 * Returns the color of the text displayed on the segment.
	 */
	virtual FLinearColor GetTextColor() const = 0;
};

} // namespace UE::Insights
