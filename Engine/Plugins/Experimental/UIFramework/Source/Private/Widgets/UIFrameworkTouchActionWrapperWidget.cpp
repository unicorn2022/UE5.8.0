// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/UIFrameworkTouchActionWrapperWidget.h"

#include "SUIFrameworkButtonWidget.h"
#include "TimerManager.h"
#include "Components/CanvasPanelSlot.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Input/CommonUIInputTypes.h"
#include "Slate/SObjectWidget.h"

UUIFrameworkTouchActionWrapperWidget::UUIFrameworkTouchActionWrapperWidget()
{
#if WITH_EDITOR
	SetDisplayLabel("Touch Action Wrapper");
#endif
}

bool UUIFrameworkTouchActionWrapperWidget::HandleTouch() const
{
	if (OnClicked.IsBound())
	{
		OnClicked.Broadcast();
		return true;
	}
	
	return false;
}

void UUIFrameworkTouchActionWrapperWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	
	if (TouchInputPreProcessor)
	{
		TouchInputPreProcessor->StopTrackingTouch();
		TouchInputPreProcessor.Reset();
	}
}

TSharedRef<SWidget> UUIFrameworkTouchActionWrapperWidget::RebuildWidget()
{
	TSharedRef<SWidget> Widget = Super::RebuildWidget();
	
	if (!TouchInputPreProcessor)
	{
		TouchInputPreProcessor = MakeShared<FUIFTouchInputPreProcessor>();
		
		TouchInputPreProcessor->SetTargetWidget(MyButton.ToWeakPtr());
		TouchInputPreProcessor->OnTrackedTouchEnded().BindUObject(this, &UUIFrameworkTouchActionWrapperWidget::HandleTouch);
		
		TouchInputPreProcessor->StartTrackingTouch();
	}
	
	return Widget;
}

FUIFTouchInputPreProcessor::FUIFTouchInputPreProcessor()
{
	WidgetClassesToIgnore.Reserve(3);
	
	WidgetClassesToIgnore.Add(FSoftObjectPath(
		TEXT("/Game/Athena/HUD/Bacchus/FortInWorldInteractionLayer.FortInWorldInteractionLayer_C")));
	WidgetClassesToIgnore.Add(FSoftObjectPath(
		TEXT("/Game/Athena/HUD/Bacchus/BacchusInteractionLayer.BacchusInteractionLayer_C")));
	WidgetClassesToIgnore.Add(FSoftObjectPath(
		TEXT("/FortniteCoreUI/HUD/Bacchus/AthenaTouchControlRegion.AthenaTouchControlRegion_C")));
}

bool FUIFTouchInputPreProcessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (!TargetWidget.IsValid())
	{
		return false;
	}
	
	if (MouseEvent.IsTouchEvent() && IsInputHittingWidget(SlateApp, MouseEvent) && OnTrackedTouchEndedDelegate.IsBound())
	{
		return OnTrackedTouchEndedDelegate.Execute();
	}
	
	return false;
}

void FUIFTouchInputPreProcessor::SetTargetWidget(TWeakPtr<SWidget> InWidget)
{
	TargetWidget = InWidget;
}

void FUIFTouchInputPreProcessor::StartTrackingTouch()
{
	FSlateApplication::Get().RegisterInputPreProcessor(AsShared());
}

void FUIFTouchInputPreProcessor::StopTrackingTouch()
{
	FSlateApplication::Get().UnregisterInputPreProcessor(AsShared());
}

bool FUIFTouchInputPreProcessor::IsInputHittingWidget(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	TSharedPtr<SWidget> PinnedTargetWidget = TargetWidget.Pin();
	if (!PinnedTargetWidget.IsValid())
		return false;
	
	const FGeometry& ButtonGeometry = PinnedTargetWidget->GetTickSpaceGeometry();
	const FVector2f TouchPosition = MouseEvent.GetScreenSpacePosition();
	
	// Do not even start the search if we don't click inside the widget
	if (!ButtonGeometry.IsUnderLocation(TouchPosition))
	{
		return false;
	}
	
	// Get the path to the top interactable widget
	// In the case of touch this will go to the bacchus interaction layers
	FWidgetPath TopWidgetPath =
		SlateApp.LocateWindowUnderMouse(TouchPosition, SlateApp.GetInteractiveTopLevelWindows());
	
	// There is nothing on top so we do not need to do all the other stuff
	if (TopWidgetPath.ContainsWidget(PinnedTargetWidget.Get()))
	{
		return true;
	}

	// Validate cached widgets, they should exist the whole game but just in case
	bool bInvalidCache = false;
	for (const TWeakPtr<SWidget>& Weak : CachedWidgetsToIgnore)
	{
		if (!Weak.IsValid())
		{
			bInvalidCache = true;
			break;
		}
	}

	if (bInvalidCache || CachedWidgetsToIgnore.IsEmpty())
	{
		CachedWidgetsToIgnore.Reset();
		
		// The interaction layers are the top ones so we start from the top
		for (int32 i = TopWidgetPath.Widgets.Num() - 1; i >= 0; i--)
		{
			TSharedRef<SWidget> Widget = TopWidgetPath.Widgets[i].Widget;
			if (Widget->GetWidgetClass().GetWidgetType() == SObjectWidget::StaticWidgetClass().GetWidgetType())
			{
				const TSharedRef<SObjectWidget> ObjectWidget = StaticCastSharedRef<SObjectWidget>(Widget);
				if (const UUserWidget* UMGWidget = ObjectWidget->GetWidgetObject(); IsValid(UMGWidget))
				{
					if (WidgetClassesToIgnore.Contains(FSoftObjectPath(UMGWidget->GetClass())))
					{
						CachedWidgetsToIgnore.Add(Widget.ToWeakPtr());
						if (CachedWidgetsToIgnore.Num() >= WidgetClassesToIgnore.Num())
							break;
					}
				}
			}
		}
	}
	
	// Get HittestGrid to do our custom test ignoring the bacchus layers
	FHittestGrid& Grid = TopWidgetPath.GetWindow()->GetHittestGrid();
	
	for (TWeakPtr<SWidget>& Widget : CachedWidgetsToIgnore)
	{
		if (TSharedPtr<SWidget> WidgetPtr = Widget.Pin())
		{
			Grid.RemoveWidget(WidgetPtr.Get());
		}
	}
	
	// Calculate the HitPath without the interaction layers
	TArray<FWidgetAndPointer> HitPath = Grid.GetBubblePath(TouchPosition, 0.0f, false);
	
	for (TWeakPtr<SWidget>& Widget : CachedWidgetsToIgnore)
	{
		if (TSharedPtr<SWidget> WidgetPtr = Widget.Pin())
		{
			WidgetPtr->Invalidate(EInvalidateWidgetReason::Paint);
		}
	}
	
	// Try to find the Widget in this path, if it is in this path then a child (or the widget itself) got the input
	for (int32 i = HitPath.Num() - 1; i >= 0; i--)
	{
		TSharedRef<SWidget> Widget = HitPath[i].Widget;

		if (Widget == PinnedTargetWidget)
		{
			return true;
		}
	}
	
	// If we don't find the widget in the path then something else got the hit
	return false;
}