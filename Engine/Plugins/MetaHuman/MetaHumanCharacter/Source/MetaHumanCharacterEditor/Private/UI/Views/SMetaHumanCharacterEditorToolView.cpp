// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditorToolView.h"

#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h" 
#include "InteractiveTool.h"
#include "ISinglePropertyView.h"
#include "Logging/StructuredLog.h"
#include "MetaHumanCharacterEditorStyle.h"
#include "MetaHumanCharacterEditorLog.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Styling/StyleColors.h"
#include "Subsystem/MetaHumanCharacterSkinMaterials.h"
#include "UI/Widgets/SUVColorPicker.h"
#include "UI/Widgets/SMetaHumanCharacterEditorColorPalettePicker.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCharacterEditorToolView"

namespace UE::MetaHuman::Private
{
	static const FName ToolViewNameID = FName(TEXT("ToolView"));
	static constexpr float CustomPropertyWidgetWidth = 120.f;
	static constexpr float DefaultPropertyWidgetWidth = 200.f;
}

SLATE_IMPLEMENT_WIDGET(SMetaHumanCharacterEditorToolView)
void SMetaHumanCharacterEditorToolView::PrivateRegisterAttributes(FSlateAttributeInitializer& InAttributeInitializer)
{
}

void SMetaHumanCharacterEditorToolView::Construct(const FArguments& InArgs, UInteractiveTool* InTool)
{
	if (!ensureMsgf(InTool, TEXT("Invalid interactive tool, can't construct the tool view correctly.")))
	{
		return;
	}

	Tool = InTool;
	ToolViewHeightRatio = 0.6f;

	ChildSlot
		[
			SAssignNew(ToolViewMainBox, SVerticalBox)
			+ SVerticalBox::Slot()
			[
				SNew(SBox)
				[
					SAssignNew(ToolViewScrollBox, SScrollBox)
					.Orientation(Orient_Vertical)
				]
			]
		];

	MakeToolView();
}

float SMetaHumanCharacterEditorToolView::GetScrollOffset() const
{
	return ToolViewScrollBox.IsValid() ? ToolViewScrollBox->GetScrollOffset() : 0.f;
}

void SMetaHumanCharacterEditorToolView::SetScrollOffset(float NewScrollOffset)
{
	if (ToolViewScrollBox.IsValid())
	{
		ToolViewScrollBox->SetScrollOffset(NewScrollOffset);
	}
}

const FName& SMetaHumanCharacterEditorToolView::GetToolViewNameID() const
{
	return UE::MetaHuman::Private::ToolViewNameID;
}

void SMetaHumanCharacterEditorToolView::OnFocusChanging(const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent)
{
	SCompoundWidget::OnFocusChanging(PreviousFocusPath, NewWidgetPath, InFocusEvent);

	/* Needed to avoid leaving a property-change FScopedTransaction open when a property widget loses focus.
	Some widgets (e.g., SNumericEntryBox) commit values through different input paths and timings,
	so we defer the check to the next tick and cancel any still-outstanding transaction. */
	if(PropertyChangeTransaction.IsValid() && PropertyChangeTransaction->IsOutstanding())
	{
		GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateSP(this, &SMetaHumanCharacterEditorToolView::CancelOutstandingTransaction));
	}
}

TSharedRef<SWidget> SMetaHumanCharacterEditorToolView::CreatePropertyNumericEntry(FProperty* InProperty, void* PropertyContainerPtr, const FString& InLabelOverride, const int32 InFractionalDigits)
{
	if (!InProperty || !PropertyContainerPtr || !CastField<FNumericProperty>(InProperty))
	{
		return SNullWidget::NullWidget;
	}

	if (CastField<FNumericProperty>(InProperty)->IsInteger())
		{
			return 
				SNew(SNumericEntryBox<int32>)
				.AllowSpin(true)
				.EditableTextBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.MinValue(this, &SMetaHumanCharacterEditorToolView::GetIntPropertyMinValue, InProperty)
				.MaxValue(this, &SMetaHumanCharacterEditorToolView::GetIntPropertyMaxValue, InProperty)
				.MinSliderValue(this, &SMetaHumanCharacterEditorToolView::GetIntPropertyMinValue, InProperty)
				.MaxSliderValue(this, &SMetaHumanCharacterEditorToolView::GetIntPropertyMaxValue, InProperty)
				.SpinBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox"))
				.Value(this, &SMetaHumanCharacterEditorToolView::GetIntPropertyValue, InProperty, PropertyContainerPtr)
				.OnBeginSliderMovement(this, &SMetaHumanCharacterEditorToolView::OnPreEditChangeProperty, InProperty, InLabelOverride)
				.OnEndSliderMovement(this, &SMetaHumanCharacterEditorToolView::OnIntPropertyValueChanged, /* bIsInteractive */ false, InProperty, PropertyContainerPtr)
				.OnValueChanged(this, &SMetaHumanCharacterEditorToolView::OnIntPropertyValueChanged, /* bIsDragging */ true, InProperty, PropertyContainerPtr)
				.OnValueCommitted(this, &SMetaHumanCharacterEditorToolView::OnIntPropertyValueCommitted, InProperty, PropertyContainerPtr)
				.Visibility(this, &SMetaHumanCharacterEditorToolView::GetPropertyVisibility, InProperty, PropertyContainerPtr)
				.IsEnabled(this, &SMetaHumanCharacterEditorToolView::IsPropertyEnabled, InProperty, PropertyContainerPtr)
				.PreventThrottling(true)
				.LabelPadding(FMargin(3))
				.LabelLocation(SNumericEntryBox<int32>::ELabelLocation::Inside)
				.Label()
				[
					SNumericEntryBox<int32>::BuildNarrowColorLabel(FLinearColor::Transparent)
				];
		}
		else
		{
			return 
				SNew(SNumericEntryBox<float>)
				.AllowSpin(true)
				.EditableTextBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.MinValue(this, &SMetaHumanCharacterEditorToolView::GetFloatPropertyMinValue, InProperty)
				.MaxValue(this, &SMetaHumanCharacterEditorToolView::GetFloatPropertyMaxValue, InProperty)
				.MinSliderValue(this, &SMetaHumanCharacterEditorToolView::GetFloatPropertyMinValue, InProperty)
				.MaxSliderValue(this, &SMetaHumanCharacterEditorToolView::GetFloatPropertyMaxValue, InProperty)
				.SpinBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox"))
				.Value(this, &SMetaHumanCharacterEditorToolView::GetFloatPropertyValue, InProperty, PropertyContainerPtr)
				.OnBeginSliderMovement(this, &SMetaHumanCharacterEditorToolView::OnPreEditChangeProperty, InProperty, InLabelOverride)
				.OnEndSliderMovement(this, &SMetaHumanCharacterEditorToolView::OnFloatPropertyValueChanged, /* bIsInteractive */ false, InProperty, PropertyContainerPtr)
				.OnValueChanged(this, &SMetaHumanCharacterEditorToolView::OnFloatPropertyValueChanged, /* bIsDragging */ true, InProperty, PropertyContainerPtr)
				.OnValueCommitted(this, &SMetaHumanCharacterEditorToolView::OnFloatPropertyValueCommited, InProperty, PropertyContainerPtr)
				.Visibility(this, &SMetaHumanCharacterEditorToolView::GetPropertyVisibility, InProperty, PropertyContainerPtr)
				.IsEnabled(this, &SMetaHumanCharacterEditorToolView::IsPropertyEnabled, InProperty, PropertyContainerPtr)
				.PreventThrottling(true)
				.MaxFractionalDigits(InFractionalDigits)
				.LinearDeltaSensitivity(1.0)
				.LabelPadding(FMargin(3))
				.LabelLocation(SNumericEntryBox<float>::ELabelLocation::Inside)
				.Label()
				[
					SNumericEntryBox<float>::BuildNarrowColorLabel(FLinearColor::Transparent)
				];
		}
}

TSharedRef<SWidget> SMetaHumanCharacterEditorToolView::CreatePropertyNumericEntryNormalized(FProperty* InProperty, void* InPropertyContainerPtr, float InMinValue, float InMaxValue, const FText& InLabelOverride)
{
	if (!InProperty || !InPropertyContainerPtr || !CastField<FNumericProperty>(InProperty))
	{
		return SNullWidget::NullWidget;
	}

	check(InMinValue < InMaxValue);

	return
		SNew(SNumericEntryBox<float>)
		.AllowSpin(true)
		.EditableTextBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
		.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
		.MinValue(0.0f)
		.MaxValue(1.0f)
		.MinSliderValue(0.0f)
		.MaxSliderValue(1.0f)
		.SpinBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox"))
		.Value(this, &SMetaHumanCharacterEditorToolView::GetFloatPropertyValueNormalized, InProperty, InPropertyContainerPtr, InMinValue, InMaxValue)
		.OnBeginSliderMovement(this, &SMetaHumanCharacterEditorToolView::OnPreEditChangeProperty, InProperty, InLabelOverride.ToString())
		.OnEndSliderMovement(this, &SMetaHumanCharacterEditorToolView::OnFloatPropertyNormalizedValueChanged, InMinValue, InMaxValue, /* bIsInteractive */ false, InProperty, InPropertyContainerPtr)
		.OnValueChanged(this, &SMetaHumanCharacterEditorToolView::OnFloatPropertyNormalizedValueChanged, InMinValue, InMaxValue, /* bIsDragging */ true, InProperty, InPropertyContainerPtr)
		.OnValueCommitted(this, &SMetaHumanCharacterEditorToolView::OnFloatPropertyNormalizedValueCommited, InMinValue, InMaxValue, InProperty, InPropertyContainerPtr)
		.PreventThrottling(true)
		.MaxFractionalDigits(2)
		.LinearDeltaSensitivity(1.0)
		.LabelPadding(FMargin(3))
		.LabelLocation(SNumericEntryBox<float>::ELabelLocation::Inside)
		.Label()
		[
			SNumericEntryBox<float>::BuildNarrowColorLabel(FLinearColor::Transparent)
		];
}

TSharedRef<SWidget> SMetaHumanCharacterEditorToolView::CreatePropertySpinBoxWidget(const FText& LabelText, FProperty* Property, void* PropertyContainerPtr, const void* DefaultContainerPtr, const int32 FractionalDigits)
{
	if (!Property || !PropertyContainerPtr)
	{
		return SNullWidget::NullWidget;
	}

	using namespace UE::MetaHuman::Private;

	return
		SNew(SVerticalBox)
		.ToolTipText(Property->GetToolTipText())

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			// SpinBox Label section
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.MinWidth(140.f)
			.Padding(10.f, 0.f)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(LabelText)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]

			// SpinBox slider section
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.FillWidth(2.f)
			.Padding(50.f, 2.f, 4.f, 2.f)
			[
				SNew(SBox)
				.WidthOverride(CustomPropertyWidgetWidth)
				[
					CreatePropertyNumericEntry(Property, PropertyContainerPtr, LabelText.ToString(), FractionalDigits)
				]
			]

			// Reset to default button section
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				CreatePropertyResetButton(Property, PropertyContainerPtr, DefaultContainerPtr)
			]
		]
		
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
			.Thickness(1.f)
		];
}

TSharedRef<SWidget> SMetaHumanCharacterEditorToolView::CreatePropertySpinBoxWidgetNormalized(FProperty* Property, void* PropertyContainerPtr, const void* DefaultContainerPtr, float InMinValue, float InMaxValue, const FText& InLabelOverride)
{
	if (!Property || !PropertyContainerPtr)
	{
		return SNullWidget::NullWidget;
	}

	if (InMinValue == InMaxValue)
	{
		const bool bHasRangeMin = Property->HasMetaData(TEXT("ClampMin"));
		const bool bHasRangeMax = Property->HasMetaData(TEXT("ClampMax"));

		if (bHasRangeMin && bHasRangeMax)
		{
			InMinValue = Property->GetFloatMetaData(TEXT("ClampMin"));
			InMaxValue = Property->GetFloatMetaData(TEXT("ClampMax"));
		}
		else
		{
			UE_LOGFMT(LogMetaHumanCharacterEditor, Warning, "Property {Property} is trying to create a normalized spinbox widget but it doesn't define ClampMin and ClampMax metadata in its declaration. Default range [0,1] will be used", Property->GetName());

			InMinValue = 0.0f;
			InMaxValue = 1.0f;
		}
	}

	check(InMinValue < InMaxValue);

	using namespace UE::MetaHuman::Private;

	return
		SNew(SVerticalBox)
		.ToolTipText(Property->GetToolTipText())

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			// SpinBox Label section
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.MinWidth(140.f)
			.Padding(10.f, 0.f)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(InLabelOverride.IsEmptyOrWhitespace() ? Property->GetDisplayNameText() : InLabelOverride)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]

			// SpinBox slider section
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.FillWidth(2.f)
			.Padding(50.f, 2.f, 4.f, 2.f)
			[
				SNew(SBox)
				.WidthOverride(CustomPropertyWidgetWidth)
				[
					CreatePropertyNumericEntryNormalized(Property, PropertyContainerPtr, InMinValue, InMaxValue, InLabelOverride)
				]
			]

			// Reset to default button section
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				CreatePropertyResetButton(Property, PropertyContainerPtr, DefaultContainerPtr)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
			.Thickness(1.f)
		];
}

TSharedRef<SWidget> SMetaHumanCharacterEditorToolView::CreatePropertyCheckBoxWidget(const FText& LabelText, FProperty* Property, void* PropertyContainerPtr, const void* DefaultContainerPtr)
{
	if (!Property || !PropertyContainerPtr)
	{
		return SNullWidget::NullWidget;
	}

	return
		SNew(SVerticalBox)
		.ToolTipText(Property->GetToolTipText())

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.MinWidth(140.f)
			.Padding(10.f, 0.f)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(LabelText)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]

			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.FillWidth(2.f)
			.Padding(50.f, 2.f, 4.f, 2.f)
			[
				SNew(SCheckBox)
				.IsChecked(this, &SMetaHumanCharacterEditorToolView::IsPropertyCheckBoxChecked, Property, PropertyContainerPtr)
				.OnCheckStateChanged(this, &SMetaHumanCharacterEditorToolView::OnPropertyCheckStateChanged, Property, PropertyContainerPtr)
			]

			// Reset to default button section
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				CreatePropertyResetButton(Property, PropertyContainerPtr, DefaultContainerPtr)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
			.Thickness(1.f)
		];
}

TSharedRef<SWidget> SMetaHumanCharacterEditorToolView::CreatePropertyColorPickerWidget(const FText& LabelText, FProperty * Property, void* PropertyContainerPtr, const void* DefaultContainerPtr)
{
	if (!Property || !PropertyContainerPtr)
	{
		return SNullWidget::NullWidget;
	}

	using namespace UE::MetaHuman::Private;

	return
		SNew(SVerticalBox)
		.ToolTipText(Property->GetToolTipText())

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			// Color Picker Label section
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.MinWidth(140.f)
			.Padding(10.f, 0.f)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(LabelText)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]

			// Color Picker block section
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.FillWidth(2.f)
			.Padding(50.f, 2.f, 4.f, 2.f)
			[
				SNew(SBox)
				.WidthOverride(CustomPropertyWidgetWidth)
				[
					SNew(SColorBlock)
					.Color(this, &SMetaHumanCharacterEditorToolView::GetColorPropertyValue, Property, PropertyContainerPtr)
					.OnMouseButtonDown(this, &SMetaHumanCharacterEditorToolView::OnColorBlockMouseButtonDown, Property, PropertyContainerPtr, LabelText.ToString())
					.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Ignore)
					.AlphaBackgroundBrush(FAppStyle::Get().GetBrush("ColorPicker.RoundedAlphaBackground"))
					.ShowBackgroundForAlpha(true)
					.CornerRadius(FVector4(2.f, 2.f, 2.f, 2.f))
				]
			]

			// Reset to default button section
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				CreatePropertyResetButton(Property, PropertyContainerPtr, DefaultContainerPtr)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
			.Thickness(1.f)
		];
}

TSharedRef<SWidget> SMetaHumanCharacterEditorToolView::CreatePropertyPalettePickerWidget(FProperty* ColorProperty,
																						 FProperty* ColorIndexProperty,
																						 void* PropertyContainerPtr,
																						 const void* DefaultContainerPtr,
																						 int32 NumPaletteColumns,
																						 int32 NumPaletteRows,
																						 const FText& LabelText,
																						 const FLinearColor& DefaultColor,
																						 UTexture2D* CustomPickerTexture,
																						 const FText& ULabelOverride,
																						 const FText& VLabelOverride)
{
	if (!ColorProperty || !PropertyContainerPtr)
	{
		return SNullWidget::NullWidget;
	}

	const FText ULabel = ULabelOverride.IsEmpty() ? LOCTEXT("ULabelOverride", "U") : ULabelOverride;
	const FText VLabel = VLabelOverride.IsEmpty() ? LOCTEXT("VLabelOverride", "V") : VLabelOverride;

	using namespace UE::MetaHuman::Private;

	return
		SNew(SVerticalBox)
		.ToolTipText(ColorProperty->GetToolTipText())

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			// Color Picker Label section
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.MinWidth(140.f)
			.Padding(10.f, 0.f)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(LabelText)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]

			// Color Picker block section
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.FillWidth(2.f)
			.Padding(50.f, 2.f, 4.f, 2.f)
			[
				SNew(SBox)
				.WidthOverride(CustomPropertyWidgetWidth)
				[
					SNew(SMetaHumanCharacterEditorColorPalettePicker)
					.PaletteDefaultColor(DefaultColor)
					.SelectedColor(this, &SMetaHumanCharacterEditorToolView::GetColorPropertyValue, ColorProperty, PropertyContainerPtr)
					.ColorPickerTexture(CustomPickerTexture)
					.PaletteLabel(LabelText)
					.PaletteColumns(NumPaletteColumns)
					.PaletteRows(NumPaletteRows)
					.ULabelOverride(ULabel)
					.VLabelOverride(VLabel)
					.UseSRGBInColorBlock(false)
					.OnPaletteColorSelectionChanged_Lambda([this, ColorProperty, ColorIndexProperty, PropertyContainerPtr](FLinearColor Color, int32 TileIndex)
															{
																const bool bIsInteractive = false;
																OnColorPropertyValueChanged(Color, bIsInteractive, ColorProperty, PropertyContainerPtr);
																OnIntPropertyValueChanged(TileIndex, bIsInteractive, ColorIndexProperty, PropertyContainerPtr);
															})
					.OnComputeVariantColor_Static(&FMetaHumanCharacterSkinMaterials::ShiftFoundationColor)
				]
			]

			// Reset to default button section
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.WidthOverride(22.0f)
				.HeightOverride(22.0f)
				.Visibility(this, &SMetaHumanCharacterEditorToolView::GetPropertyResetVisibility, ColorProperty, PropertyContainerPtr, DefaultContainerPtr)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.OnClicked_Lambda([this, ColorProperty, ColorIndexProperty, PropertyContainerPtr, DefaultContainerPtr]()
						{
							OnPropertyResetToDefaultClicked(ColorProperty, PropertyContainerPtr, DefaultContainerPtr);
							OnPropertyResetToDefaultClicked(ColorIndexProperty, PropertyContainerPtr, DefaultContainerPtr);

							return FReply::Handled();
						})
					.ContentPadding(0.0f)
					.IsFocusable(false)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
			.Thickness(1.f)
		];
}

TSharedRef<SWidget> SMetaHumanCharacterEditorToolView::CreatePropertyUVColorPickerWidget(FProperty* PropertyU,
																						 FProperty* PropertyV,
																						 void* PropertyContainerPtr,
																						 const void* DefaultContainerPtr,
																						 const FText& ColorPickerLabel,
																						 TNotNull<UTexture2D*> ColorPickerTexture,
																						 const FText& ULabelOverride /*= FText::GetEmpty()*/,
																						 const FText& VLabelOverride /*= FText::GetEmpty()*/)
{
	if (!PropertyU || !PropertyV || !PropertyContainerPtr)
	{
		return SNullWidget::NullWidget;
	}

	using namespace UE::MetaHuman::Private;

	return 
		SNew(SVerticalBox)
		.ToolTipText(PropertyU->GetToolTipText())

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			// Color Picker Label
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.MinWidth(140.f)
			.Padding(10.f, 0.f)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(ColorPickerLabel)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]

			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.FillWidth(2.f)
			.Padding(50.f, 2.f, 4.f, 2.f)
			[
				SNew(SBox)
				.WidthOverride(CustomPropertyWidgetWidth)
				[
					SNew(SUVColorPicker)
					.UV_Lambda([this, PropertyU, PropertyV, PropertyContainerPtr]
					{
						return FVector2f
						(
							GetFloatPropertyValue(PropertyU, PropertyContainerPtr).GetValue(),
							GetFloatPropertyValue(PropertyV, PropertyContainerPtr).GetValue()
						);
					})
					.OnUVChanged_Lambda([this, PropertyU, PropertyV, PropertyContainerPtr](const FVector2f& InUV, bool bIsDragging)
					{
						OnPreEditChangeProperty(PropertyU);
						OnPreEditChangeProperty(PropertyV);
						OnFloatPropertyValueChanged(InUV.X, bIsDragging, PropertyU, PropertyContainerPtr);
						OnFloatPropertyValueChanged(InUV.Y, bIsDragging, PropertyV, PropertyContainerPtr);
					})
					.ColorPickerLabel(ColorPickerLabel)
					.ULabelOverride(ULabelOverride)
					.VLabelOverride(VLabelOverride)
					.ColorPickerTexture(ColorPickerTexture)
				]		
			]

			// Reset to default button section
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.WidthOverride(22.0f)
				.HeightOverride(22.0f)
				.Visibility_Lambda([this, PropertyU, PropertyV, PropertyContainerPtr, DefaultContainerPtr]()
				{
					const bool bIsVisible = 
						GetPropertyResetVisibility(PropertyU, PropertyContainerPtr, DefaultContainerPtr) == EVisibility::Visible ||
						GetPropertyResetVisibility(PropertyV, PropertyContainerPtr, DefaultContainerPtr) == EVisibility::Visible;

					return bIsVisible ? EVisibility::Visible : EVisibility::Hidden;
				})
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.OnClicked_Lambda([this, PropertyU, PropertyV, PropertyContainerPtr, DefaultContainerPtr]()
					{
						OnPropertyResetToDefaultClicked(PropertyU, PropertyContainerPtr, DefaultContainerPtr);
						OnPropertyResetToDefaultClicked(PropertyV, PropertyContainerPtr, DefaultContainerPtr);
						
						return FReply::Handled();
					})
					.ContentPadding(0.0f)
					.IsFocusable(false)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
			]
		]
		
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
			.Thickness(1.f)
		];
}

TSharedRef<SWidget> SMetaHumanCharacterEditorToolView::CreatePropertyDefaultWidget(const FText& LabelText, FProperty* Property, UObject* PropertyContainerObject, FNotifyHook* NotifyHook)
{
	if (!Property || !PropertyContainerObject)
	{
		return SNullWidget::NullWidget;
	}

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FSinglePropertyParams PropertyParams;
	PropertyParams.NamePlacement = EPropertyNamePlacement::Hidden;
	PropertyParams.bHideResetToDefault = true;
	PropertyParams.NotifyHook = NotifyHook;

	const TSharedPtr<ISinglePropertyView> PropertyView = PropertyEditorModule.CreateSingleProperty(PropertyContainerObject, Property->NamePrivate, PropertyParams);
	if (!PropertyView.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	using namespace UE::MetaHuman::Private;

	return 
		SNew(SVerticalBox)
		.ToolTipText(Property->GetToolTipText())

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			// Label section
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.MinWidth(140.f)
			.Padding(10.f, 0.f)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(LabelText)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]

			// Single Property view section
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.FillWidth(2.f)
			.Padding(50.f, 2.f, 4.f, 2.f)
			[
				SNew(SBox)
				.WidthOverride(DefaultPropertyWidgetWidth)
				[
					PropertyView.ToSharedRef()
				]
			]

			// Reset to default button section
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				CreatePropertyResetButton(Property, PropertyContainerObject, PropertyContainerObject->GetClass()->GetDefaultObject())
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
			.Thickness(1.f)
		];
}

TSharedRef<SWidget> SMetaHumanCharacterEditorToolView::CreatePropertyResetButton(FProperty* Property, void* PropertyContainerPtr, const void* DefaultContainerPtr)
{
	return
		SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.WidthOverride(22.0f)
		.HeightOverride(22.0f)
		.Visibility(this, &SMetaHumanCharacterEditorToolView::GetPropertyResetVisibility, Property, PropertyContainerPtr, DefaultContainerPtr)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.OnClicked(this, &SMetaHumanCharacterEditorToolView::OnPropertyResetToDefaultClicked, Property, PropertyContainerPtr, DefaultContainerPtr)
			.ContentPadding(0.0f)
			.IsFocusable(false)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];
}

ECheckBoxState SMetaHumanCharacterEditorToolView::IsPropertyCheckBoxChecked(FProperty* Property, void* PropertyContainerPtr) const
{
	if (Property && PropertyContainerPtr)
	{
		const bool bIsChecked = GetBoolPropertyValue(Property, PropertyContainerPtr);
		return bIsChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	return ECheckBoxState::Undetermined;
}

void SMetaHumanCharacterEditorToolView::OnPropertyCheckStateChanged(ECheckBoxState CheckState, FProperty* Property, void* PropertyContainerPtr)
{
	const bool bIsChecked = CheckState == ECheckBoxState::Checked;
	if (Property && PropertyContainerPtr)
	{
		OnBoolPropertyValueChanged(bIsChecked, Property, PropertyContainerPtr);
	}
}

FReply SMetaHumanCharacterEditorToolView::OnColorBlockMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FProperty* Property, void* PropertyContainerPtr, const FString Label)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	FColorPickerArgs Args;
	Args.bIsModal = false;
	Args.bOnlyRefreshOnMouseUp = false;
	Args.bOnlyRefreshOnOk = false;
	Args.bUseAlpha = true;
	Args.bOpenAsMenu = true;
	Args.InitialColor = GetColorPropertyValue(Property, PropertyContainerPtr);
	Args.OnInteractivePickBegin = FSimpleDelegate::CreateSP(this, &SMetaHumanCharacterEditorToolView::OnPreEditChangeProperty, Property, Label);
	Args.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(this, &SMetaHumanCharacterEditorToolView::OnColorPropertyValueChanged, /* bIsInteractive */ true, Property, PropertyContainerPtr);
	Args.OnInteractivePickEnd = FSimpleDelegate::CreateSP(this, &SMetaHumanCharacterEditorToolView::OnPostEditChangeProperty, Property, /* bIsInteractive */ false);
	Args.OnColorPickerCancelled = FOnColorPickerCancelled::CreateSP(this, &SMetaHumanCharacterEditorToolView::OnColorPropertyValueChanged, /* bIsInteractive */ false, Property, PropertyContainerPtr);

	OpenColorPicker(Args);

	return FReply::Handled();
}

TOptional<int32> SMetaHumanCharacterEditorToolView::GetIntPropertyValue(FProperty* Property, void* PropertyContainerPtr) const
{
	TOptional<int32> PropertyValue;
	const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property);
	if (NumericProperty && PropertyContainerPtr)
	{
		const int32* PropertyValuePtr = NumericProperty->ContainerPtrToValuePtr<int32>(PropertyContainerPtr);
		PropertyValue = NumericProperty->GetSignedIntPropertyValue(PropertyValuePtr);
	}

	return PropertyValue;
}

TOptional<int32> SMetaHumanCharacterEditorToolView::GetIntPropertyMinValue(FProperty* Property) const
{
	const FName Key = TEXT("UIMin");

	TOptional<int32> PropertyMinValue;
	const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property);
	if (NumericProperty && NumericProperty->HasMetaData(Key))
	{
		PropertyMinValue = NumericProperty->GetIntMetaData(Key);
	}

	return PropertyMinValue;
}

TOptional<int32> SMetaHumanCharacterEditorToolView::GetIntPropertyMaxValue(FProperty* Property) const
{
	const FName Key = TEXT("UIMax");

	TOptional<int32> PropertyMaxValue;
	const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property);
	if (NumericProperty && NumericProperty->HasMetaData(Key))
	{
		PropertyMaxValue = NumericProperty->GetIntMetaData(Key);
	}

	return PropertyMaxValue;
}

TOptional<float> SMetaHumanCharacterEditorToolView::GetFloatPropertyValue(FProperty* Property, void* PropertyContainerPtr) const
{
	TOptional<float> PropertyValue;
	const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property);
	if (NumericProperty && PropertyContainerPtr)
	{
		const float* PropertyValuePtr = NumericProperty->ContainerPtrToValuePtr<float>(PropertyContainerPtr);
		PropertyValue = NumericProperty->GetFloatingPointPropertyValue(PropertyValuePtr);
	}

	return PropertyValue;
}

TOptional<float> SMetaHumanCharacterEditorToolView::GetFloatPropertyValueNormalized(FProperty* InProperty, void* InPropertyContainerPtr, float InMinValue, float InMaxValue) const
{
	TOptional<float> PropertyValue = GetFloatPropertyValue(InProperty, InPropertyContainerPtr);
	if (PropertyValue.IsSet())
	{
		return (PropertyValue.GetValue() - InMinValue) / (InMaxValue - InMinValue);
	}
	return PropertyValue;
}

TOptional<float> SMetaHumanCharacterEditorToolView::GetFloatPropertyMinValue(FProperty* Property) const
{
	TOptional<float> PropertyMinValue;
	const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property);
	if (NumericProperty)
	{
		const FString& MinValueAsString = Property->GetMetaData(TEXT("UIMin"));
		PropertyMinValue = FCString::Atof(*MinValueAsString);
	}

	return PropertyMinValue;
}

TOptional<float> SMetaHumanCharacterEditorToolView::GetFloatPropertyMaxValue(FProperty* Property) const
{
	TOptional<float> PropertyMaxValue;
	const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property);
	if (NumericProperty)
	{
		const FString& MaxValueAsString = Property->GetMetaData(TEXT("UIMax"));
		PropertyMaxValue = FCString::Atof(*MaxValueAsString);
	}

	return PropertyMaxValue;
}

bool SMetaHumanCharacterEditorToolView::GetBoolPropertyValue(FProperty* Property, void* PropertyContainerPtr) const
{
	bool PropertyValue = false;
	const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property);
	if (BoolProperty && PropertyContainerPtr)
	{
		PropertyValue = BoolProperty->GetPropertyValue_InContainer(PropertyContainerPtr);
	}

	return PropertyValue;
}

FLinearColor SMetaHumanCharacterEditorToolView::GetColorPropertyValue(FProperty* Property, void* PropertyContainerPtr) const
{
	FLinearColor PropertyValue = FLinearColor::White;
	const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
	if (StructProperty && StructProperty->Struct->GetFName() == NAME_LinearColor && PropertyContainerPtr)
	{
		const FLinearColor* ColorPtr = StructProperty->ContainerPtrToValuePtr<FLinearColor>(PropertyContainerPtr);
		PropertyValue = ColorPtr ? *ColorPtr : FLinearColor::White;
	}

	return PropertyValue;
}

void SMetaHumanCharacterEditorToolView::OnPreEditChangeProperty(FProperty* Property, const FString Label)
{
	UInteractiveToolPropertySet* ToolProperties = GetToolProperties();
	if (!ToolProperties || !Property)
	{
		return;
	}

	if (!PropertyChangeTransaction.IsValid())
	{
		const FString PropertyName = Label.IsEmpty() ? Property->GetName() : Label;
		const FText TransactionText = FText::Format(LOCTEXT("ToolPropertyChangeTransaction", "Edit {0}"), FText::FromString(PropertyName));
		PropertyChangeTransaction = MakeUnique<FScopedTransaction>(TransactionText);
	}
	
	ToolProperties->PreEditChange(Property);
}

void SMetaHumanCharacterEditorToolView::OnPostEditChangeProperty(FProperty* Property, bool bIsInteractive)
{
	UInteractiveToolPropertySet* ToolProperties = GetToolProperties();
	if (!ToolProperties || !Property)
	{
		return;
	}

	const EPropertyChangeType::Type PropertyFlags = bIsInteractive ? EPropertyChangeType::Interactive : EPropertyChangeType::ValueSet;
	FPropertyChangedEvent PropertyChangedEvent(Property, PropertyFlags);
	ToolProperties->PostEditChangeProperty(PropertyChangedEvent);

	if (!bIsInteractive && PropertyChangeTransaction.IsValid() && PropertyChangeTransaction->IsOutstanding())
	{
		PropertyChangeTransaction.Reset();
	}
}

void SMetaHumanCharacterEditorToolView::OnIntPropertyValueChanged(int32 Value, bool bIsInteractive, FProperty* Property, void* PropertyContainerPtr)
{
	UInteractiveToolPropertySet* ToolProperties = GetToolProperties();
	const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property);
	if (ToolProperties && NumericProperty && PropertyContainerPtr)
	{
		if(bIsInteractive && !PropertyChangeTransaction.IsValid())
		{
			OnPreEditChangeProperty(Property, Property->GetName());
		}

		const TOptional<int32> MinValue = GetIntPropertyMinValue(Property);
		const TOptional<int32> MaxValue = GetIntPropertyMaxValue(Property);

		if (MinValue.IsSet())
		{
			Value = FMath::Max(Value, MinValue.GetValue());
		}

		if (MaxValue.IsSet())
		{
			Value = FMath::Min(Value, MaxValue.GetValue());
		}

		int32* PropertyValuePtr = NumericProperty->ContainerPtrToValuePtr<int32>(PropertyContainerPtr);
		NumericProperty->SetIntPropertyValue(PropertyValuePtr, static_cast<int64>(Value));

		OnPostEditChangeProperty(Property, bIsInteractive);
	}
}

void SMetaHumanCharacterEditorToolView::OnFloatPropertyValueChanged(const float Value, bool bIsInteractive, FProperty* Property, void* PropertyContainerPtr)
{
	UInteractiveToolPropertySet* ToolProperties = GetToolProperties();
	const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property);
	if (ToolProperties && NumericProperty && PropertyContainerPtr)
	{
		if (bIsInteractive && !PropertyChangeTransaction.IsValid())
		{
			OnPreEditChangeProperty(Property, Property->GetName());
		}

		const TOptional<float> MinValue = GetFloatPropertyMinValue(Property);
		const TOptional<float> MaxValue = GetFloatPropertyMaxValue(Property);
		float ClampedValue = Value;
		if (MinValue.IsSet() && MaxValue.IsSet())
		{
			ClampedValue = FMath::Clamp(Value, MinValue.GetValue(), MaxValue.GetValue());
		}
		else if (MinValue.IsSet())
		{
			ClampedValue = FMath::Max(Value, MinValue.GetValue());
		}
		else if (MaxValue.IsSet())
		{
			ClampedValue = FMath::Min(Value, MaxValue.GetValue());
		}

		float* PropertyValuePtr = NumericProperty->ContainerPtrToValuePtr<float>(PropertyContainerPtr);
		NumericProperty->SetFloatingPointPropertyValue(PropertyValuePtr, ClampedValue);

		OnPostEditChangeProperty(Property, bIsInteractive);
	}
}

void SMetaHumanCharacterEditorToolView::OnIntPropertyValueCommitted(int32 Value, ETextCommit::Type Type, FProperty* Property, void* PropertyContainerPtr)
{
	const bool bIsInteractive = false;
	OnIntPropertyValueChanged(Value, bIsInteractive, Property, PropertyContainerPtr);
}

void SMetaHumanCharacterEditorToolView::OnFloatPropertyNormalizedValueChanged(float Value, float InMinValue, float InMaxValue, bool bIsInteractive, FProperty* InProperty, void* InPropertyContainerPtr)
{
	UInteractiveToolPropertySet* ToolProperties = GetToolProperties();
	const FNumericProperty* NumericProperty = CastField<FNumericProperty>(InProperty);
	if (ToolProperties && NumericProperty && InPropertyContainerPtr)
	{
		const float ActualValue = FMath::Lerp(InMinValue, InMaxValue, Value);

		float* PropertyValuePtr = NumericProperty->ContainerPtrToValuePtr<float>(InPropertyContainerPtr);
		NumericProperty->SetFloatingPointPropertyValue(PropertyValuePtr, ActualValue);

		OnPostEditChangeProperty(InProperty, bIsInteractive);
	}
}

void SMetaHumanCharacterEditorToolView::OnFloatPropertyValueCommited(const float Value, ETextCommit::Type Type, FProperty* Property, void* PropertyContainerPtr)
{
	const bool bIsInteractive = false;
	OnFloatPropertyValueChanged(Value, bIsInteractive, Property, PropertyContainerPtr);
}

void SMetaHumanCharacterEditorToolView::OnFloatPropertyNormalizedValueCommited(float InValue, ETextCommit::Type InType, float InMinValue, float InMaxValue, FProperty* InProperty, void* InPropertyContainerPtr)
{
	const bool bIsInteractive = false;
	OnFloatPropertyNormalizedValueChanged(InValue, InMinValue, InMaxValue, bIsInteractive, InProperty, InPropertyContainerPtr);
}

void SMetaHumanCharacterEditorToolView::OnBoolPropertyValueChanged(const bool Value, FProperty* Property, void* PropertyContainerPtr)
{
	UInteractiveToolPropertySet* ToolProperties = GetToolProperties();
	const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property);
	if (ToolProperties && BoolProperty && PropertyContainerPtr)
	{
		FString PropertyName = Property->GetName();
		PropertyName.RemoveFromStart(TEXT("b"));
		const FText TransactionText = FText::Format(LOCTEXT("ToolBoolPropertyChangeTransaction", "Edit {0}"), FText::FromString(PropertyName));
		const FScopedTransaction OnEnumPropertyChangedTransaction = FScopedTransaction(TransactionText);
		ToolProperties->PreEditChange(Property);

		BoolProperty->SetPropertyValue_InContainer(PropertyContainerPtr, Value);

		FPropertyChangedEvent PropertyChangedEvent(Property, EPropertyChangeType::ValueSet);
		ToolProperties->PostEditChangeProperty(PropertyChangedEvent);
	}
}

void SMetaHumanCharacterEditorToolView::OnEnumPropertyValueChanged(uint8 Value, FProperty* Property, void* PropertyContainerPtr)
{
	UInteractiveToolPropertySet* ToolProperties = GetToolProperties();
	const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property);
	if (ToolProperties && EnumProperty && PropertyContainerPtr)
	{
		const FText TransactionText = FText::Format(LOCTEXT("ToolEnumPropertyChangeTransaction", "Edit {0}"), FText::FromString(Property->GetName()));
		const FScopedTransaction OnEnumPropertyChangedTransaction = FScopedTransaction(TransactionText);
		ToolProperties->PreEditChange(Property);

		int64* PropertyValuePtr = EnumProperty->ContainerPtrToValuePtr<int64>(PropertyContainerPtr);
		EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(PropertyValuePtr, static_cast<int64>(Value));

		FPropertyChangedEvent PropertyChangedEvent(Property, EPropertyChangeType::ValueSet);
		ToolProperties->PostEditChangeProperty(PropertyChangedEvent);
	}
}

void SMetaHumanCharacterEditorToolView::OnColorPropertyValueChanged(FLinearColor Color, bool bIsInteractive, FProperty* Property, void* PropertyContainerPtr)
{
	UInteractiveToolPropertySet* ToolProperties = GetToolProperties();
	const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
	if (!ToolProperties || !StructProperty || !PropertyContainerPtr)
	{
		return;
	}

	if (StructProperty->Struct->GetFName() != NAME_LinearColor)
	{
		return;
	}

	FLinearColor* ColorPtr = StructProperty->ContainerPtrToValuePtr<FLinearColor>(PropertyContainerPtr);
	if (ColorPtr)
	{
		*ColorPtr = Color;

		OnPostEditChangeProperty(Property, bIsInteractive);
	}
}

FReply SMetaHumanCharacterEditorToolView::OnPropertyResetToDefaultClicked(FProperty* Property, void* PropertyContainerPtr, const void* DefaultContainerPtr)
{
	if (Property && PropertyContainerPtr && DefaultContainerPtr)
	{
		OnPreEditChangeProperty(Property, Property->GetName());
		Property->CopyCompleteValue_InContainer(PropertyContainerPtr, DefaultContainerPtr);
		OnPostEditChangeProperty(Property, /* bIsInteractive */ false);
	}

	return FReply::Handled();
}

EVisibility SMetaHumanCharacterEditorToolView::GetPropertyResetVisibility(FProperty* Property, void* PropertyContainerPtr, const void* DefaultContainerPtr) const
{
	bool bIsVisible = false;
	if (Property && PropertyContainerPtr && DefaultContainerPtr)
	{
		bIsVisible = !Property->Identical_InContainer(PropertyContainerPtr, DefaultContainerPtr, 0, 0);
	}

	return bIsVisible ? EVisibility::Visible : EVisibility::Hidden;
}

EVisibility SMetaHumanCharacterEditorToolView::GetPropertyVisibility(FProperty* Property, void* PropertyContainerPtr) const
{
	return EVisibility::Visible;
}

bool SMetaHumanCharacterEditorToolView::IsPropertyEnabled(FProperty* Property, void* PropertyContainerPtr) const
{
	return true;
}

void SMetaHumanCharacterEditorToolView::CancelOutstandingTransaction()
{
	if (PropertyChangeTransaction.IsValid() && PropertyChangeTransaction->IsOutstanding())
	{
		PropertyChangeTransaction->Cancel();
		PropertyChangeTransaction.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
