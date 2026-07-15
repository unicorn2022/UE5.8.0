// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditorSkinToolView.h"

#include "IDetailsView.h"
#include "InteractiveTool.h"
#include "IStructureDetailsView.h"
#include "MetaHumanCharacterEditorStyle.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Tools/MetaHumanCharacterEditorSkinTool.h"
#include "UI/Widgets/SMetaHumanCharacterEditorAccentRegionsPanel.h"
#include "UI/Widgets/SMetaHumanCharacterEditorTileView.h"
#include "UI/Widgets/SMetaHumanCharacterEditorToolPanel.h"
#include "UI/Widgets/SMetaHumanCharacterEditorTextComboBox.h"
#include "UObject/StructOnScope.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "SWarningOrErrorBox.h"
#include "MetaHumanCharacterEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCharacterEditorSkinToolView"

namespace UE::MetaHuman::Private
{
	// Skin texture attribute names come from texture_attributes.json (data-driven) and
	// are not gatherable as LOCTEXT directly. Map the known attribute names to localized
	// FText here so the Skin section labels appear translated in non-English locales.
	// Unknown attributes fall back to their raw string so future additions still work.
	static FText GetLocalizedSkinAttributeName(const FString& AttributeName)
	{
		static const TMap<FString, FText> LocalizedNames = {
			{ TEXT("Face Wrinkles"), LOCTEXT("SkinAttr_FaceWrinkles", "Face Wrinkles") },
			{ TEXT("Face Stubble"),  LOCTEXT("SkinAttr_FaceStubble",  "Face Stubble") },
			{ TEXT("Face Marks"),    LOCTEXT("SkinAttr_FaceMarks",    "Face Marks") },
		};

		if (const FText* Found = LocalizedNames.Find(AttributeName))
		{
			return *Found;
		}
		return FText::FromString(AttributeName);
	}
}

void SMetaHumanCharacterEditorSkinToolView::Construct(const FArguments& InArgs, UMetaHumanCharacterEditorSkinTool* InTool)
{
	SMetaHumanCharacterEditorToolView::Construct(SMetaHumanCharacterEditorToolView::FArguments(), InTool);
}

UInteractiveToolPropertySet* SMetaHumanCharacterEditorSkinToolView::GetToolProperties() const
{
	const UMetaHumanCharacterEditorSkinTool* SkinTool = Cast<UMetaHumanCharacterEditorSkinTool>(Tool);
	return IsValid(SkinTool) ? SkinTool->GetSkinToolProperties() : nullptr;
}

void SMetaHumanCharacterEditorSkinToolView::MakeToolView()
{
	if (ToolViewScrollBox.IsValid())
	{
		ToolViewScrollBox->AddSlot()
			.VAlign(VAlign_Top)
			[
				SNew(SVerticalBox)


				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateSkinToolViewSkinSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateSkinToolViewFrecklesSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateSkinToolViewAccentsSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateTextureSourceSection()
				]
			];
	}
}

void SMetaHumanCharacterEditorSkinToolView::NotifyPreChange(FProperty* PropertyAboutToChange)
{
	OnPreEditChangeProperty(PropertyAboutToChange, PropertyAboutToChange->GetName());
}

void SMetaHumanCharacterEditorSkinToolView::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	const bool bIsInteractive = PropertyChangedEvent.ChangeType & EPropertyChangeType::Interactive;
	OnPostEditChangeProperty(PropertyThatChanged, bIsInteractive);
}

TSharedRef<SWidget> SMetaHumanCharacterEditorSkinToolView::CreateSkinToolViewSkinSection()
{
	UMetaHumanCharacterEditorSkinToolProperties* SkinToolProperties = Cast<UMetaHumanCharacterEditorSkinToolProperties>(GetToolProperties());
	void* SkinProperties = IsValid(SkinToolProperties) ? &SkinToolProperties->Skin : nullptr;
	if (!SkinProperties)
	{
		return SNullWidget::NullWidget;
	}

	void* FaceEvaluationProperties = IsValid(SkinToolProperties) ? &SkinToolProperties->FaceEvaluationSettings : nullptr;
	if (!FaceEvaluationProperties)
	{
		return SNullWidget::NullWidget;
	}

	FProperty* UProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, U));
	FProperty* VProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, V));
	FProperty* ShowTopUnderwearProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, bShowTopUnderwear));
	FProperty* BodyTextureIndexProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, BodyTextureIndex));
	FProperty* FaceTextureIndexProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, FaceTextureIndex));
	FProperty* RoughnessProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, Roughness));
	FProperty* HighFrequencyDeltaProperty = FMetaHumanCharacterFaceEvaluationSettings::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterFaceEvaluationSettings, HighFrequencyDelta));
	FProperty* SkinFilterEnabledProperty = UMetaHumanCharacterEditorSkinToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorSkinToolProperties, bIsSkinFilterEnabled));
	FProperty* SkinFilterIndexProperty = UMetaHumanCharacterEditorSkinToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorSkinToolProperties, SkinFilterIndex));

	TNotNull<UMetaHumanCharacterEditorSubsystem*> MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	int32 NumTextureAttributes = MetaHumanCharacterSubsystem->GetFaceTextureAttributeMap().NumAttributes();
	if (AttributeValueNames.Num() != NumTextureAttributes)
	{
		AttributeValueNames.Reset();
		for (int32 Idx = 0; Idx < NumTextureAttributes; ++Idx)
		{
			AttributeValueNames.Push(TArray<TSharedPtr<FString>>());
			AttributeValueNames.Last().Push(MakeShared<FString>("---"));
			for (int32 NameIdx = 0; NameIdx < MetaHumanCharacterSubsystem->GetFaceTextureAttributeMap().GetAttributeValueNames(Idx).Num(); ++NameIdx)
			{
				AttributeValueNames.Last().Push(MakeShared<FString>(MetaHumanCharacterSubsystem->GetFaceTextureAttributeMap().GetAttributeValueNames(Idx)[NameIdx]));
			}
		}
	}

	const void* SkinToolPropertiesDefault = SkinToolProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorSkinToolProperties>();
	const void* SkinPropertiesDefault = &SkinToolProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorSkinToolProperties>()->Skin;
	const void* FaceEvaluationPropertiesDefault = &SkinToolProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorSkinToolProperties>()->FaceEvaluationSettings;

	TSharedRef<SVerticalBox> VerticalBox = SNew(SVerticalBox)
		.IsEnabled(this, &SMetaHumanCharacterEditorSkinToolView::IsSkinEditEnabled);

	// Skin Tone Picker section
	VerticalBox->AddSlot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Fill)
		.AutoHeight()
		[
			CreatePropertyUVColorPickerWidget(UProperty,
											  VProperty,
											  SkinProperties,
											  SkinPropertiesDefault,
											  LOCTEXT("SkinTonePicker", "Skin Tone"),
											  MetaHumanCharacterSubsystem->GetOrCreateSkinToneTexture().Get())
		];

	// Body texture index section. CreatePropertySpinBoxWidget builds its own label which does not
	// consult IsPropertyEnabled, so wrap the whole row so the label also greys out when rigged.
	VerticalBox->AddSlot()
		.AutoHeight()
		[
			SNew(SBox)
			.IsEnabled_UObject(SkinToolProperties, &UMetaHumanCharacterEditorSkinToolProperties::CanEditRigRestrictedProperties)
			[
				CreatePropertySpinBoxWidget(BodyTextureIndexProperty->GetDisplayNameText(), BodyTextureIndexProperty, SkinProperties, SkinPropertiesDefault, 4)
			]
		];

	// Texture spinbox section. The filter checkbox does not consult IsPropertyEnabled, so gate it
	// explicitly on the rigging state.
	VerticalBox->AddSlot()
		.AutoHeight()
		[
			SNew(SBox)
			.IsEnabled_UObject(SkinToolProperties, &UMetaHumanCharacterEditorSkinToolProperties::CanEditRigRestrictedProperties)
			[
				CreatePropertyCheckBoxWidget(SkinFilterEnabledProperty->GetDisplayNameText(), SkinFilterEnabledProperty, SkinToolProperties, SkinToolPropertiesDefault)
			]
		];

	for (int32 Idx = 0; Idx < NumTextureAttributes; ++Idx)
	{
		VerticalBox->AddSlot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				// Attribute combos drive FaceTextureIndex, so disable them while rigged.
				.IsEnabled_UObject(SkinToolProperties, &UMetaHumanCharacterEditorSkinToolProperties::CanEditRigRestrictedProperties)
				.Visibility_Lambda([this]() -> EVisibility 
					{
						if (UMetaHumanCharacterEditorSkinToolProperties* SkinToolProperties = Cast<UMetaHumanCharacterEditorSkinToolProperties>(GetToolProperties()))
						{
							return SkinToolProperties->bIsSkinFilterEnabled ? EVisibility::Visible : EVisibility::Collapsed;
						}
						return EVisibility::Collapsed;
					})
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.FillWidth(.3f)
				.Padding(14.f, 0.f)
				[
					SNew(STextBlock)
					.Text(UE::MetaHuman::Private::GetLocalizedSkinAttributeName(MetaHumanCharacterSubsystem->GetFaceTextureAttributeMap().GetAttributeName(Idx)))
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				]

				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				.FillWidth(.7f)
				.Padding(4.f, 2.f, 40.f, 2.f)
				[
					SNew(SMetaHumanCharacterEditorTextComboBox, AttributeValueNames[Idx], AttributeValueNames[Idx][0])
					.OnSelectionChanged_Lambda([this, Idx](int32 ItemIdx) 
					{
						if (UMetaHumanCharacterEditorSkinToolProperties* SkinToolProperties = Cast<UMetaHumanCharacterEditorSkinToolProperties>(GetToolProperties()))
						{
							SkinToolProperties->SkinFilterValues[Idx] = ItemIdx - 1;
							FProperty* SkinFilterProperty = UMetaHumanCharacterEditorSkinToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorSkinToolProperties, SkinFilterValues));
							OnPostEditChangeProperty(SkinFilterProperty, /*bIsInteractive*/false);
						}
					})
				]
			];

		VerticalBox->AddSlot()
			.AutoHeight()
			[
				SNew(SSeparator)
				.Thickness(1.f)
				.Visibility_Lambda([this]() -> EVisibility {
						if (UMetaHumanCharacterEditorSkinToolProperties* SkinToolProperties = Cast<UMetaHumanCharacterEditorSkinToolProperties>(GetToolProperties()))
						{
							return SkinToolProperties->bIsSkinFilterEnabled ? EVisibility::Visible : EVisibility::Collapsed;
						}
						return EVisibility::Collapsed;
					})
			];
	}

	VerticalBox->AddSlot()
		.AutoHeight()
		[
			SNew(SVerticalBox)
			// Face Filter Index drives FaceTextureIndex so disable the whole row (label included)
			// when rigged.
			.IsEnabled_UObject(SkinToolProperties, &UMetaHumanCharacterEditorSkinToolProperties::CanEditRigRestrictedProperties)
			.Visibility_Lambda([this]() -> EVisibility {
				if (UMetaHumanCharacterEditorSkinToolProperties* SkinToolProperties = Cast<UMetaHumanCharacterEditorSkinToolProperties>(GetToolProperties()))
				{
					return SkinToolProperties->bIsSkinFilterEnabled ? EVisibility::Visible : EVisibility::Collapsed;
				}
				return EVisibility::Hidden;
			})
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				// SpinBox Label section
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.FillWidth(.3f)
				.Padding(10.f, 0.f)
				[
					SNew(STextBlock)
					.Text(SkinFilterIndexProperty->GetDisplayNameText())
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				]

				// SpinBox slider section
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				.FillWidth(.7f)
				.Padding(4.f, 2.f, 40.f, 2.f)
				[
					CreatePropertyNumericEntry(SkinFilterIndexProperty, SkinToolProperties, TEXT("Face Filter Index"), 4)
				]
			]
		];

	// Face texture index section. Wrap so the row label greys out with the spin box when rigged.
	VerticalBox->AddSlot()
		.AutoHeight()
		[
			SNew(SBox)
			.IsEnabled_UObject(SkinToolProperties, &UMetaHumanCharacterEditorSkinToolProperties::CanEditRigRestrictedProperties)
			[
				CreatePropertySpinBoxWidget(FaceTextureIndexProperty->GetDisplayNameText(), FaceTextureIndexProperty, SkinProperties, SkinPropertiesDefault, 4)
			]
		];

	// Texture Position Offset (HighFrequencyDelta) — shown directly after Face Texture Index since
	// it also feeds the synthesized face texture.
	VerticalBox->AddSlot()
		.AutoHeight()
		[
			SNew(SBox)
			.IsEnabled_UObject(SkinToolProperties, &UMetaHumanCharacterEditorSkinToolProperties::CanEditRigRestrictedProperties)
			[
				CreatePropertySpinBoxWidget(HighFrequencyDeltaProperty->GetDisplayNameText(), HighFrequencyDeltaProperty, FaceEvaluationProperties, FaceEvaluationPropertiesDefault)
			]
		];

	// Roughness spinbox section
	VerticalBox->AddSlot()
		.AutoHeight()
		[
			CreatePropertySpinBoxWidgetNormalized(RoughnessProperty, SkinProperties, SkinPropertiesDefault, 0.85f, 1.15f)
		];

	// Show underwear
	VerticalBox->AddSlot()
		.AutoHeight()
		[
			CreatePropertyCheckBoxWidget(ShowTopUnderwearProperty->GetDisplayNameText(), ShowTopUnderwearProperty, SkinProperties, SkinPropertiesDefault)
		];

	// Hands & Feet Subsection
	VerticalBox->AddSlot()
		.AutoHeight()
		[
			CreateSkinToolViewHandsAndFeetSkinSubSection()
		];

	TSharedRef<SWidget> SkinSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("SkinSectionLabel", "Skin"))
		.Content()
		[
			VerticalBox
		];
		
	return SkinSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorSkinToolView::CreateSkinToolViewHandsAndFeetSkinSubSection()
{
	UMetaHumanCharacterEditorSkinToolProperties* SkinToolProperties = Cast<UMetaHumanCharacterEditorSkinToolProperties>(GetToolProperties());
	void* SkinProperties = IsValid(SkinToolProperties) ? &SkinToolProperties->Skin : nullptr;
	if (!SkinProperties)
	{
		return SNullWidget::NullWidget;
	}

	// Palm
	FProperty* PalmLightnessProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, PalmLightness));
	FProperty* PalmTintProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, PalmTint));
	FProperty* PalmCavityDarknessProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, PalmCavityDarkness));

	// Fingernail
	FProperty* FingernailTintColorProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, FingernailTintColor));
	FProperty* FingernailTintIntensityProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, FingernailTintIntensity));
	FProperty* FingernailMetallicProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, FingernailMetallic));
	FProperty* FingernailRoughnessProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, FingernailRoughness));

	// Toenail
	FProperty* ToenailTintColorProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, ToenailTintColor));
	FProperty* ToenailTintIntensityProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, ToenailTintIntensity));
	FProperty* ToenailMetallicProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, ToenailMetallic));
	FProperty* ToenailRoughnessProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, ToenailRoughness));

	const void* SkinPropertiesDefault = &SkinToolProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorSkinToolProperties>()->Skin;
	const void* SkinToolPropertiesDefault = SkinToolProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorSkinToolProperties>();
	FProperty* ShowHandsProperty = UMetaHumanCharacterEditorSkinToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorSkinToolProperties, bShowHands));

	TSharedRef<SVerticalBox> VerticalBox = SNew(SVerticalBox)
		.IsEnabled(this, &SMetaHumanCharacterEditorSkinToolView::IsSkinEditEnabled);

	VerticalBox->AddSlot()
		.AutoHeight()
		[
			SNew(SBox)
			.IsEnabled_Lambda([SkinToolProperties, ShowHandsProperty]()
			{
				return IsValid(SkinToolProperties) && SkinToolProperties->CanEditChange(ShowHandsProperty);
			})
			[
				CreatePropertyCheckBoxWidget(ShowHandsProperty->GetDisplayNameText(), ShowHandsProperty, SkinToolProperties, SkinToolPropertiesDefault)
			]
		];

	VerticalBox->AddSlot()
		.AutoHeight()
		[
			CreatePropertySpinBoxWidget(PalmLightnessProperty->GetDisplayNameText(), PalmLightnessProperty, SkinProperties, SkinPropertiesDefault)
		];

	VerticalBox->AddSlot()
		.AutoHeight()
		[
			CreatePropertySpinBoxWidget(PalmTintProperty->GetDisplayNameText(), PalmTintProperty, SkinProperties, SkinPropertiesDefault)
		];

	VerticalBox->AddSlot()
		.AutoHeight()
		[
			CreatePropertySpinBoxWidgetNormalized(PalmCavityDarknessProperty, SkinProperties, SkinPropertiesDefault)
		];

	VerticalBox->AddSlot()
		.MinHeight(24.f)
		.Padding(2.f, 0.f)
		.AutoHeight()
		[
			CreatePropertyColorPickerWidget(FingernailTintColorProperty->GetDisplayNameText(), FingernailTintColorProperty, SkinProperties, SkinPropertiesDefault)
		];

	VerticalBox->AddSlot()
		.AutoHeight()
		[
			CreatePropertySpinBoxWidget(FingernailTintIntensityProperty->GetDisplayNameText(), FingernailTintIntensityProperty, SkinProperties, SkinPropertiesDefault)
		];

	VerticalBox->AddSlot()
		.AutoHeight()
		[
			CreatePropertySpinBoxWidget(FingernailMetallicProperty->GetDisplayNameText(), FingernailMetallicProperty, SkinProperties, SkinPropertiesDefault)
		];

	VerticalBox->AddSlot()
		.AutoHeight()
		[
			CreatePropertySpinBoxWidget(FingernailRoughnessProperty->GetDisplayNameText(), FingernailRoughnessProperty, SkinProperties, SkinPropertiesDefault)
		];

	VerticalBox->AddSlot()
		.MinHeight(24.f)
		.Padding(2.f, 0.f)
		.AutoHeight()
		[
			CreatePropertyColorPickerWidget(ToenailTintColorProperty->GetDisplayNameText(), ToenailTintColorProperty, SkinProperties, SkinPropertiesDefault)
		];

	VerticalBox->AddSlot()
		.AutoHeight()
		[
			CreatePropertySpinBoxWidget(ToenailTintIntensityProperty->GetDisplayNameText(), ToenailTintIntensityProperty, SkinProperties, SkinPropertiesDefault)
		];

	VerticalBox->AddSlot()
		.AutoHeight()
		[
			CreatePropertySpinBoxWidget(ToenailMetallicProperty->GetDisplayNameText(), ToenailMetallicProperty, SkinProperties, SkinPropertiesDefault)
		];

	VerticalBox->AddSlot()
		.AutoHeight()
		[
			CreatePropertySpinBoxWidget(ToenailRoughnessProperty->GetDisplayNameText(), ToenailRoughnessProperty, SkinProperties, SkinPropertiesDefault)
		];

	TSharedRef<SWidget> HandsAndFeetSkinSubSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("HandsAndFeetSkinSubSectionLabel", "Hands & Feet"))
		.RoundedBorders(false)
		.HierarchyLevel(EMetaHumanCharacterEditorPanelHierarchyLevel::Middle)
		.Content()
		[
			VerticalBox
		];

	return HandsAndFeetSkinSubSectionWidget;
}


TSharedRef<SWidget> SMetaHumanCharacterEditorSkinToolView::CreateSkinToolViewFrecklesSection()
{
	UMetaHumanCharacterEditorSkinToolProperties* SkinToolProperties = Cast<UMetaHumanCharacterEditorSkinToolProperties>(GetToolProperties());
	void* FrecklesProperties = IsValid(SkinToolProperties) ? &SkinToolProperties->Freckles : nullptr;
	if (!FrecklesProperties)
	{
		return SNullWidget::NullWidget;
	}

	FProperty* DensityProperty = FMetaHumanCharacterFrecklesProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterFrecklesProperties, Density));
	FProperty* StrengthProperty = FMetaHumanCharacterFrecklesProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterFrecklesProperties, Strength));
	FProperty* SaturationProperty = FMetaHumanCharacterFrecklesProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterFrecklesProperties, Saturation));
	FProperty* ToneShiftProperty = FMetaHumanCharacterFrecklesProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterFrecklesProperties, ToneShift));
	FProperty* MaskProperty = FMetaHumanCharacterFrecklesProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterFrecklesProperties, Mask));

	const void* FrecklesPropertiesDefault = &SkinToolProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorSkinToolProperties>()->Freckles;
	const TSharedRef<SWidget> FrecklesSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("FrecklesSectionLabel", "Freckles"))
		.Content()
		[
			SNew(SVerticalBox)
			.IsEnabled(this, &SMetaHumanCharacterEditorSkinToolView::IsEditEnabled)

			// Density spinbox section
			+ SVerticalBox::Slot()
			.MinHeight(24.f)
			.Padding(2.f, 0.f)
			.AutoHeight()
			[
				CreatePropertySpinBoxWidget(DensityProperty->GetDisplayNameText(), DensityProperty, FrecklesProperties, FrecklesPropertiesDefault)
			]

			// Strength spinbox section
			+ SVerticalBox::Slot()
			.MinHeight(24.f)
			.Padding(2.f, 0.f)
			.AutoHeight()
			[
				CreatePropertySpinBoxWidget(StrengthProperty->GetDisplayNameText(), StrengthProperty, FrecklesProperties, FrecklesPropertiesDefault)
			]

			// Saturation spinbox section
			+ SVerticalBox::Slot()
			.MinHeight(24.f)
			.Padding(2.f, 0.f)
			.AutoHeight()
			[
				CreatePropertySpinBoxWidget(SaturationProperty->GetDisplayNameText(), SaturationProperty, FrecklesProperties, FrecklesPropertiesDefault)
			]

			// ToneShift spinbox section
			+SVerticalBox::Slot()
			.MinHeight(24.f)
			.Padding(2.f, 0.f)
			.AutoHeight()
			[
				CreatePropertySpinBoxWidget(ToneShiftProperty->GetDisplayNameText(), ToneShiftProperty, FrecklesProperties, FrecklesPropertiesDefault)
			]

			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(4.f)
			.AutoHeight()
			[
				SNew(SMetaHumanCharacterEditorTileView<EMetaHumanCharacterFrecklesMask>)
				.OnGetSlateBrush(this, &SMetaHumanCharacterEditorSkinToolView::GetFreckesSectionBrush)
				.OnSelectionChanged(this, &SMetaHumanCharacterEditorSkinToolView::OnEnumPropertyValueChanged, MaskProperty, FrecklesProperties)
				.InitiallySelectedItem(SkinToolProperties->Freckles.Mask)
			]
		];

	return FrecklesSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorSkinToolView::CreateSkinToolViewAccentsSection()
{
	FProperty* RednessProperty = FMetaHumanCharacterAccentRegionProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterAccentRegionProperties, Redness));
	FProperty* SaturationProperty = FMetaHumanCharacterAccentRegionProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterAccentRegionProperties, Saturation));
	FProperty* LightnessProperty = FMetaHumanCharacterAccentRegionProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterAccentRegionProperties, Lightness));

	const TSharedRef<SWidget> AccentsSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("AccentsSectionLabel", "Accents"))
		.Content()
		[
			SNew(SVerticalBox)
			.IsEnabled(this, &SMetaHumanCharacterEditorSkinToolView::IsEditEnabled)

			//Accent Regions panel section
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(8.f)
			.AutoHeight()
			[
				SAssignNew(AccentRegionsPanel, SMetaHumanCharacterEditorAccentRegionsPanel)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSeparator)
				.Thickness(1.f)
			]

			// Redness spinbox section
			+SVerticalBox::Slot()
			.MinHeight(24.f)
			.Padding(2.f, 0.f)
			.AutoHeight()
			[
				CreateAccentRegionPropertySpinBoxWidget(RednessProperty->GetDisplayNameText(), RednessProperty)
			]

			// Saturation spinbox section
			+ SVerticalBox::Slot()
			.MinHeight(24.f)
			.Padding(2.f, 0.f)
			.AutoHeight()
			[
				CreateAccentRegionPropertySpinBoxWidget(SaturationProperty->GetDisplayNameText(), SaturationProperty)
			]

			// Lightness spinbox section
			+ SVerticalBox::Slot()
			.MinHeight(24.f)
			.Padding(2.f, 0.f)
			.AutoHeight()
			[
				CreateAccentRegionPropertySpinBoxWidget(LightnessProperty->GetDisplayNameText(), LightnessProperty)
			]
		];

	return AccentsSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorSkinToolView::CreateTextureSourceSection()
{
	UMetaHumanCharacterEditorSkinToolProperties* SkinToolProperties = Cast<UMetaHumanCharacterEditorSkinToolProperties>(GetToolProperties());
	if (!SkinToolProperties)
	{
		return SNullWidget::NullWidget;
	}

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.NotifyHook = this;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	FStructureDetailsViewArgs StructureViewArgs;
	StructureViewArgs.bShowObjects = true;
	StructureViewArgs.bShowAssets = true;
	StructureViewArgs.bShowClasses = true;
	StructureViewArgs.bShowInterfaces = true;

	TSharedRef<FStructOnScope> TextureSourcesResolutionsStruct = MakeShared<FStructOnScope>(FMetaHumanCharacterTextureSourceResolutions::StaticStruct(), (uint8*) &SkinToolProperties->DesiredTextureSourcesResolutions);
	TSharedRef<IStructureDetailsView> TextureSourcesDetailsView = PropertyEditorModule.CreateStructureDetailView(DetailsViewArgs, StructureViewArgs, TextureSourcesResolutionsStruct);
	check(TextureSourcesDetailsView->GetWidget().IsValid());

	// Refresh on undo/redo.
	SkinToolProperties->OnSkinToolCommandChangeApplied.AddSPLambda(this,
		[WeakDetailsView = TWeakPtr<IStructureDetailsView>(TextureSourcesDetailsView)]()
		{
			if (const TSharedPtr<IStructureDetailsView> PinnedView = WeakDetailsView.Pin())
			{
				if (IDetailsView* InnerView = PinnedView->GetDetailsView())
				{
					InnerView->InvalidateCachedState();
				}
			}
		});

	TSharedRef<SHorizontalBox> ResolutionsPresetBox = SNew(SHorizontalBox);

	for (ERequestTextureResolution Resolution : TEnumRange<ERequestTextureResolution>())
	{
		ResolutionsPresetBox->AddSlot()
		[
			SNew(SCheckBox)
			.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox"))
			.Type(ESlateCheckBoxType::ToggleButton)
			.IsChecked_Lambda([SkinToolProperties, Resolution]
			{			
				return SkinToolProperties->DesiredTextureSourcesResolutions.AreAllResolutionsEqualTo(Resolution) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([SkinToolProperties, Resolution, WeakDetailsView = TWeakPtr<IStructureDetailsView>(TextureSourcesDetailsView)](ECheckBoxState NewState)
			{
				SkinToolProperties->DesiredTextureSourcesResolutions.SetAllResolutionsTo(Resolution);

				FProperty* TextureResolutionsProperty = UMetaHumanCharacterEditorSkinToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorSkinToolProperties, DesiredTextureSourcesResolutions));

				FPropertyChangedEvent ChangeEvent(TextureResolutionsProperty, EPropertyChangeType::ValueSet);
				SkinToolProperties->PostEditChangeProperty(ChangeEvent);

				// Refresh after direct memory write.
				if (const TSharedPtr<IStructureDetailsView> PinnedView = WeakDetailsView.Pin())
				{
					if (IDetailsView* InnerView = PinnedView->GetDetailsView())
					{
						InnerView->InvalidateCachedState();
					}
				}
			})
			[
				SNew(STextBlock)
				.Text(UEnum::GetDisplayValueAsText(Resolution))
			]
		];
	}

	const TSharedRef<SWidget> TextureSourcesSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("TextureManagementSectionLabel", "Textures Sources"))
		.Content()
		[
			SNew(SVerticalBox)

			// Resolution Presets
			+ SVerticalBox::Slot()
			.MinHeight(24.f)
			.AutoHeight()
			[
				ResolutionsPresetBox
			]

			// Texture Sources section
			+ SVerticalBox::Slot()
			.MinHeight(24.f)
			.AutoHeight()
			[
				TextureSourcesDetailsView->GetWidget().ToSharedRef()
			]
		];

	return TextureSourcesSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorSkinToolView::CreateAccentRegionPropertySpinBoxWidget(const FText LabelText, FProperty* Property, const int32 FractionalDigits)
{
	if (!Property)
	{
		return SNullWidget::NullWidget;
	}

	return
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			// Slider Label section
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
				SNew(SBox)
				.WidthOverride(120.f)
				[
					SNew(SNumericEntryBox<float>)
					.AllowSpin(true)
					.EditableTextBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.MinValue(this, &SMetaHumanCharacterEditorSkinToolView::GetFloatPropertyMinValue, Property)
					.MaxValue(this, &SMetaHumanCharacterEditorSkinToolView::GetFloatPropertyMaxValue, Property)
					.MinSliderValue(this, &SMetaHumanCharacterEditorSkinToolView::GetFloatPropertyMinValue, Property)
					.MaxSliderValue(this, &SMetaHumanCharacterEditorSkinToolView::GetFloatPropertyMaxValue, Property)
					.SpinBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox"))
					.Value(this, &SMetaHumanCharacterEditorSkinToolView::GetAccentRegionFloatPropertyValue, Property)
					.OnBeginSliderMovement(this, &SMetaHumanCharacterEditorSkinToolView::OnPreEditChangeProperty, Property, LabelText.ToString())
					.OnEndSliderMovement(this, &SMetaHumanCharacterEditorSkinToolView::OnAccentRegionFloatPropertyValueChanged, /* bIsInteractive */ false, Property)
					.OnValueChanged(this, &SMetaHumanCharacterEditorSkinToolView::OnAccentRegionFloatPropertyValueChanged, /* bIsDragging */ true, Property)
					.PreventThrottling(true)
					.MaxFractionalDigits(FractionalDigits)
					.LinearDeltaSensitivity(1.0)
					.Delta(.001f)
					.LabelPadding(FMargin(3))
					.LabelLocation(SNumericEntryBox<float>::ELabelLocation::Inside)
					.Label()
					[
						SNumericEntryBox<float>::BuildNarrowColorLabel(FLinearColor::Transparent)
					]
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
				.Visibility(this, &SMetaHumanCharacterEditorSkinToolView::GetAccentRegionPropertyResetVisibility, Property)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.OnClicked(this, &SMetaHumanCharacterEditorSkinToolView::OnAccentRegionPropertyResetToDefaultClicked, Property)
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

void* SMetaHumanCharacterEditorSkinToolView::GetAccentRegionPropertyContainerFromSelection() const
{
	UMetaHumanCharacterEditorSkinToolProperties* SkinToolProperties = Cast<UMetaHumanCharacterEditorSkinToolProperties>(GetToolProperties());
	if (!SkinToolProperties || !AccentRegionsPanel.IsValid())
	{
		return nullptr;
	}

	const EMetaHumanCharacterAccentRegion SelectedRegion = AccentRegionsPanel->GetSelectedRegion();
	switch (SelectedRegion)
	{
	case EMetaHumanCharacterAccentRegion::Scalp:
		return &SkinToolProperties->Accents.Scalp;
	case EMetaHumanCharacterAccentRegion::Forehead:
		return &SkinToolProperties->Accents.Forehead;
	case EMetaHumanCharacterAccentRegion::Nose:
		return &SkinToolProperties->Accents.Nose;
	case EMetaHumanCharacterAccentRegion::UnderEye:
		return &SkinToolProperties->Accents.UnderEye;
	case EMetaHumanCharacterAccentRegion::Ears:
		return &SkinToolProperties->Accents.Ears;
	case EMetaHumanCharacterAccentRegion::Cheeks:
		return &SkinToolProperties->Accents.Cheeks;
	case EMetaHumanCharacterAccentRegion::Lips:
		return &SkinToolProperties->Accents.Lips;
	case EMetaHumanCharacterAccentRegion::Chin:
		return &SkinToolProperties->Accents.Chin;
	default:
		return nullptr;
	}
}

const void* SMetaHumanCharacterEditorSkinToolView::GetAccentRegionDefaultPropertyContainerFromSelection() const
{
	UMetaHumanCharacterEditorSkinToolProperties* SkinToolProperties = Cast<UMetaHumanCharacterEditorSkinToolProperties>(GetToolProperties());
	if (!SkinToolProperties || !AccentRegionsPanel.IsValid())
	{
		return nullptr;
	}

	const UMetaHumanCharacterEditorSkinToolProperties* SkinPropertiesDefault = SkinToolProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorSkinToolProperties>();
	const EMetaHumanCharacterAccentRegion SelectedRegion = AccentRegionsPanel->GetSelectedRegion();
	switch (SelectedRegion)
	{
	case EMetaHumanCharacterAccentRegion::Scalp:
		return &SkinPropertiesDefault->Accents.Scalp;
	case EMetaHumanCharacterAccentRegion::Forehead:
		return &SkinPropertiesDefault->Accents.Forehead;
	case EMetaHumanCharacterAccentRegion::Nose:
		return &SkinPropertiesDefault->Accents.Nose;
	case EMetaHumanCharacterAccentRegion::UnderEye:
		return &SkinPropertiesDefault->Accents.UnderEye;
	case EMetaHumanCharacterAccentRegion::Ears:
		return &SkinPropertiesDefault->Accents.Ears;
	case EMetaHumanCharacterAccentRegion::Cheeks:
		return &SkinPropertiesDefault->Accents.Cheeks;
	case EMetaHumanCharacterAccentRegion::Lips:
		return &SkinPropertiesDefault->Accents.Lips;
	case EMetaHumanCharacterAccentRegion::Chin:
		return &SkinPropertiesDefault->Accents.Chin;
	default:
		return nullptr;
	}
}

TOptional<float> SMetaHumanCharacterEditorSkinToolView::GetAccentRegionFloatPropertyValue(FProperty* Property) const
{
	TOptional<float> PropertyValue;
	void* PropertyContainerPtr = GetAccentRegionPropertyContainerFromSelection();
	if (PropertyContainerPtr)
	{
		PropertyValue = GetFloatPropertyValue(Property, PropertyContainerPtr);
	}

	return PropertyValue;
}

void SMetaHumanCharacterEditorSkinToolView::OnAccentRegionFloatPropertyValueChanged(const float Value, bool bIsDragging, FProperty* Property)
{
	void* PropertyContainerPtr = GetAccentRegionPropertyContainerFromSelection();
	if (PropertyContainerPtr)
	{
		OnFloatPropertyValueChanged(Value, bIsDragging, Property, PropertyContainerPtr);
	}
}

FReply SMetaHumanCharacterEditorSkinToolView::OnAccentRegionPropertyResetToDefaultClicked(FProperty* Property)
{
	void* PropertyContainerPtr = GetAccentRegionPropertyContainerFromSelection();
	const void* DefaultPropertyContainerPtr = GetAccentRegionDefaultPropertyContainerFromSelection();
	if (Property && PropertyContainerPtr && DefaultPropertyContainerPtr)
	{
		OnPropertyResetToDefaultClicked(Property, PropertyContainerPtr, DefaultPropertyContainerPtr);
	}

	return FReply::Handled();
}

EVisibility SMetaHumanCharacterEditorSkinToolView::GetAccentRegionPropertyResetVisibility(FProperty* Property) const
{
	void* PropertyContainerPtr = GetAccentRegionPropertyContainerFromSelection();
	const void* DefaultPropertyContainerPtr = GetAccentRegionDefaultPropertyContainerFromSelection();
	if (Property && PropertyContainerPtr && DefaultPropertyContainerPtr)
	{
		return GetPropertyResetVisibility(Property, PropertyContainerPtr, DefaultPropertyContainerPtr);
	}

	return EVisibility::Hidden;
}

void SMetaHumanCharacterEditorSkinToolView::OnSkinUVChanged(const FVector2f& UV, bool bIsDragging)
{
	UMetaHumanCharacterEditorSkinToolProperties* ToolProperties = Cast<UMetaHumanCharacterEditorSkinToolProperties>(GetToolProperties());
	void* SkinProperties = IsValid(ToolProperties) ? &ToolProperties->Skin : nullptr;
	if (ToolProperties)
	{
		FProperty* UProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, U));
		FProperty* VProperty = FMetaHumanCharacterSkinProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, V));

		OnFloatPropertyValueChanged(UV.X, bIsDragging, UProperty, SkinProperties);
		OnFloatPropertyValueChanged(UV.Y, bIsDragging, VProperty, SkinProperties);
	}
}

const FSlateBrush* SMetaHumanCharacterEditorSkinToolView::GetFreckesSectionBrush(uint8 InItem)
{
	const FString FrecklesMaskName = StaticEnum<EMetaHumanCharacterFrecklesMask>()->GetAuthoredNameStringByValue(InItem);
	const FString FrecklesMaskBrushName = FString::Format(TEXT("Skin.Freckles.{0}"), { FrecklesMaskName });
	return FMetaHumanCharacterEditorStyle::Get().GetBrush(*FrecklesMaskBrushName);
}

bool SMetaHumanCharacterEditorSkinToolView::IsEditEnabled() const
{
	const UMetaHumanCharacterEditorSkinToolProperties* SkinToolProperties = Cast<UMetaHumanCharacterEditorSkinToolProperties>(GetToolProperties());
	return SkinToolProperties != nullptr;
}

bool SMetaHumanCharacterEditorSkinToolView::IsSkinEditEnabled() const
{
	TNotNull<UMetaHumanCharacterEditorSubsystem*> MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	return MetaHumanCharacterSubsystem->IsTextureSynthesisEnabled();
}

EVisibility SMetaHumanCharacterEditorSkinToolView::GetPropertyVisibility(FProperty* Property, void* PropertyContainerPtr) const
{
	return SMetaHumanCharacterEditorToolView::GetPropertyVisibility(Property, PropertyContainerPtr);
}

bool SMetaHumanCharacterEditorSkinToolView::IsPropertyEnabled(FProperty* Property, void* PropertyContainerPtr) const
{
	// Lock rig-restricted properties (face/body texture indices, texture-attribute filter,
	// Texture Position Offset) while the character is rigged.
	if (const UMetaHumanCharacterEditorSkinToolProperties* RiggedCheckProperties = Cast<UMetaHumanCharacterEditorSkinToolProperties>(GetToolProperties()))
	{
		if (UMetaHumanCharacterEditorSkinToolProperties::IsRigRestrictedProperty(Property)
			&& !RiggedCheckProperties->CanEditRigRestrictedProperties())
		{
			return false;
		}
	}

	if (Property->GetName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorSkinToolProperties, SkinFilterIndex))
	{
		UMetaHumanCharacterEditorSkinToolProperties* ToolProperties = Cast<UMetaHumanCharacterEditorSkinToolProperties>(GetToolProperties());
		if (IsValid(ToolProperties) && ToolProperties->bIsSkinFilterEnabled)
		{
			const UMetaHumanCharacterEditorSkinTool* SkinTool = Cast<UMetaHumanCharacterEditorSkinTool>(Tool);
			return SkinTool->IsFilteredFaceTextureIndicesValid();
		}
		return false;
	}
	else if (Property->GetName() == GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterSkinProperties, FaceTextureIndex))
	{
		UMetaHumanCharacterEditorSkinToolProperties* ToolProperties = Cast<UMetaHumanCharacterEditorSkinToolProperties>(GetToolProperties());
		return IsValid(ToolProperties) ? !ToolProperties->bIsSkinFilterEnabled : true;
	}
	return SMetaHumanCharacterEditorToolView::IsPropertyEnabled(Property, PropertyContainerPtr);
}

#undef LOCTEXT_NAMESPACE
