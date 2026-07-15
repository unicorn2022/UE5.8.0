// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FShaderAuditSession;

class SInspectShaderSessionWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInspectShaderSessionWidget) {}
		SLATE_ARGUMENT(TSharedPtr<FShaderAuditSession>, Session)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetSession(TSharedPtr<FShaderAuditSession> InSession);

private:
	void RebuildFromSession();
	TSharedPtr<FShaderAuditSession> Session;
};
