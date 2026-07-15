// Copyright Epic Games, Inc. All Rights Reserved.

#include "Util/WidgetReference.h"

#include "LogVCamCore.h"
#include "Util/WidgetTreeUtils.h"

#include "Blueprint/WidgetTree.h"

UWidget* FChildWidgetReference::ResolveWidget(UUserWidget& OwnerWidget) const
{
	UWidget* ResolvedTemplate = Template.LoadSynchronous();
	if (!ResolvedTemplate)
	{
		return nullptr;
	}
	
	UWidgetTree* WidgetTree = OwnerWidget.WidgetTree;
#if WITH_EDITOR
	if (!WidgetTree && OwnerWidget.HasAnyFlags(RF_ClassDefaultObject))
	{
		WidgetTree = UE::VCamCore::GetWidgetTreeThroughBlueprintAsset(OwnerWidget);
	}
#endif

	const bool bCanFindWidget = WidgetTree != nullptr;;
	UE_CLOGF(!bCanFindWidget, LogVCamCore, Warning, "Failed to get tree for widget %ls", *OwnerWidget.GetPathName());
	ensureMsgf(ResolvedTemplate->GetTypedOuter<UWidgetTree>() && ResolvedTemplate->GetTypedOuter<UWidgetTree>()->HasAnyFlags(RF_DefaultSubObject), TEXT("Template does not point to any template widget!"));

	const FName WidgetName = ResolvedTemplate->GetFName();
	return bCanFindWidget
		? WidgetTree->FindWidget<UWidget>(WidgetName)
		: nullptr;
}
