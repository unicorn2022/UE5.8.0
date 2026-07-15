// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FShaderAuditSession;

class SShaderAuditOverviewCard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShaderAuditOverviewCard) {}
		SLATE_ARGUMENT(TSharedPtr<FShaderAuditSession>, Session)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
