// Copyright Epic Games, Inc. All Rights Reserved.

#include "UIFrameworkCustomButtonWidget.h"

#include "CommonInputSubsystem.h"
#include "Components/ButtonSlot.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Widgets/Input/SButton.h"

UUIFrameworkCustomButtonWidget::UUIFrameworkCustomButtonWidget()
{
	DefaultContentWidgetClass = FSoftObjectPath(TEXT("/Game/Valkyrie/UMG/UEFN_Custom_Button_Default_Content.UEFN_Custom_Button_Default_Content_C"));
#if WITH_EDITOR
	SetDisplayLabel("Custom Button");
#endif
}

TSharedRef<SWidget> UUIFrameworkCustomButtonWidget::RebuildWidget()
{
	TSharedRef<SWidget> Widget = Super::RebuildWidget();

	OnClicked.AddUniqueDynamic(this, &UUIFrameworkCustomButtonWidget::HandleButtonClick);
	
	OnHovered.AddUniqueDynamic(this, &UUIFrameworkCustomButtonWidget::HandleButtonHover);
	OnUnhovered.AddUniqueDynamic(this, &UUIFrameworkCustomButtonWidget::HandleButtonUnhover);
	
	OnTouchLongPress.BindUObject(this, &UUIFrameworkCustomButtonWidget::HandleButtonHighlight);
	OnTouchEnded.BindUObject(this, &UUIFrameworkCustomButtonWidget::HandleButtonUnhighlight);
	
	AsyncLoadDefaultWidgetClass();
	
	return Widget;
}

void UUIFrameworkCustomButtonWidget::LocalCreateDefaultUMGWidget()
{
	if (UClass* Class = DefaultContentWidgetClass.Get())
	{
		if (LocalDefaultUMGWidget && LocalDefaultUMGWidget->GetClass() != Class)
		{
			LocalDefaultUMGWidget->RemoveFromParent();
			LocalDefaultUMGWidget = nullptr;
		}

		if (!LocalDefaultUMGWidget)
		{
			if (Class->IsChildOf(UUserWidget::StaticClass()))
			{
				if (UUserWidget* OwningUserWidget = GetTypedOuter<UUserWidget>())
				{
					LocalDefaultUMGWidget = CreateWidget(OwningUserWidget, Class);
				}
			}
			else
			{
				check(Class->IsChildOf(UWidget::StaticClass()));
				LocalDefaultUMGWidget = NewObject<UWidget>(this, Class, FName(), RF_Transient);
			}

			if (LocalDefaultUMGWidget)
			{
#if WITH_EDITOR
				if (IsDesignTime() && MyButton.IsValid())
				{
					const UPanelSlot* PanelSlot = GetContentSlot();
					if (!IsValid(PanelSlot) || PanelSlot->GetContent() == nullptr)
					{
						MyButton->SetContent(LocalDefaultUMGWidget->TakeWidget());
					}
				}
				else
#endif
				{
					const UPanelSlot* PanelSlot = GetContentSlot();
					if (!IsValid(PanelSlot) || PanelSlot->GetContent() == nullptr)
					{
						SetContent(LocalDefaultUMGWidget);

						if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(LocalDefaultUMGWidget->Slot))
						{
							ButtonSlot->SetPadding(FMargin());
							ButtonSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
							ButtonSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
						}
					}
				}
			}
		}
	}
}

void UUIFrameworkCustomButtonWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	if (WidgetClassStreamableHandle.IsValid())
	{
		WidgetClassStreamableHandle->CancelHandle();
		WidgetClassStreamableHandle.Reset();
	}	
}

TSharedPtr<FStreamableHandle> UUIFrameworkCustomButtonWidget::AsyncLoadDefaultWidgetClass()
{
	if (WidgetClassStreamableHandle.IsValid())
	{
		// Check that we're loading the correct class
		TArray<FSoftObjectPath> LoadingWidgets;
		constexpr bool bIncludeChildHandles = false;
		WidgetClassStreamableHandle->GetRequestedAssets(LoadingWidgets, bIncludeChildHandles);
		if (LoadingWidgets.Contains(DefaultContentWidgetClass.ToSoftObjectPath()))
		{
			return WidgetClassStreamableHandle;
		}

		// WidgetClass changed while we were loading
		WidgetClassStreamableHandle->CancelHandle();
		WidgetClassStreamableHandle.Reset();
	}

#if WITH_EDITOR
	if (IsDesignTime())
	{
		if (!DefaultContentWidgetClass.Get() && !DefaultContentWidgetClass.IsNull())
		{
			UAssetManager::GetStreamableManager().LoadSynchronous(DefaultContentWidgetClass.ToSoftObjectPath());
		}
		
		LocalCreateDefaultUMGWidget();

		return nullptr;
	}
#endif

	TWeakObjectPtr<ThisClass> WeakSelf = this;
	WidgetClassStreamableHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		DefaultContentWidgetClass.ToSoftObjectPath()
		, [WeakSelf]()
		{
			if (ThisClass* StrongSelf = WeakSelf.Get())
			{
				StrongSelf->LocalCreateDefaultUMGWidget();
				StrongSelf->WidgetClassStreamableHandle.Reset();
			}
		}
	, FStreamableManager::AsyncLoadHighPriority, false, false, TEXT("UUIFrameworkCustomButtonWidget::WidgetClass"));

	return WidgetClassStreamableHandle;
}

void UUIFrameworkCustomButtonWidget::HandleButtonClick()
{
	if (OnButtonClicked.IsBound())
	{
		OnButtonClicked.Broadcast();
	}
}

void UUIFrameworkCustomButtonWidget::HandleButtonHighlight()
{
	if (OnButtonHighlight.IsBound())
	{
		OnButtonHighlight.Broadcast();
	}
}

void UUIFrameworkCustomButtonWidget::HandleButtonUnhighlight()
{
	if (OnButtonUnhighlight.IsBound())
	{
		OnButtonUnhighlight.Broadcast();
	}
}

void UUIFrameworkCustomButtonWidget::HandleButtonHover()
{
	if (const APlayerController* PlayerController = GetOwningPlayer())
	{
		if (const UCommonInputSubsystem* CommonInputSubsystem = UCommonInputSubsystem::Get(PlayerController->GetLocalPlayer()))
		{
			if (CommonInputSubsystem->GetCurrentInputType() == ECommonInputType::Touch)
			{
				return;
			}
		}
	}
	
	HandleButtonHighlight();
}

void UUIFrameworkCustomButtonWidget::HandleButtonUnhover()
{
	if (const APlayerController* PlayerController = GetOwningPlayer())
	{
		if (const UCommonInputSubsystem* CommonInputSubsystem = UCommonInputSubsystem::Get(PlayerController->GetLocalPlayer()))
		{
			if (CommonInputSubsystem->GetCurrentInputType() == ECommonInputType::Touch)
			{
				return;
			}
		}
	}
	
	HandleButtonUnhighlight();
}
