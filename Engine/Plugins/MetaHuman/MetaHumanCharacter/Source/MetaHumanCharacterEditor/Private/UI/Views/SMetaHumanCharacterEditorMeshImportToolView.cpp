// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditorMeshImportToolView.h"

#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "MetaHumanCharacterEditorStyle.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/StyleColors.h"
#include "Tools/MetaHumanCharacterEditorMeshImportTool.h"
#include "UI/Widgets/SMetaHumanCharacterEditorToolPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCharacterEditorMeshImportToolView"

SMetaHumanCharacterEditorMeshImportToolView::~SMetaHumanCharacterEditorMeshImportToolView()
{
	if (AdvancedFeaturesWindow.IsValid())
	{
		AdvancedFeaturesWindow->RequestDestroyWindow();
	}
}

void SMetaHumanCharacterEditorMeshImportToolView::Construct(const FArguments& InArgs, UMetaHumanCharacterEditorMeshImportTool* InTool)
{
	SMetaHumanCharacterEditorToolView::Construct(SMetaHumanCharacterEditorToolView::FArguments(), InTool);
}

UInteractiveToolPropertySet* SMetaHumanCharacterEditorMeshImportToolView::GetToolProperties() const
{
	const UMetaHumanCharacterEditorMeshImportTool* MeshImportTool = Cast<UMetaHumanCharacterEditorMeshImportTool>(Tool);
	return IsValid(MeshImportTool) ? MeshImportTool->GetMeshImportProperties() : nullptr;
}

void SMetaHumanCharacterEditorMeshImportToolView::MakeToolView()
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
					CreateMeshImportToolbarSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateMeshImportWarningTextSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateMeshImportLoadCustomMeshSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateMeshImportLoadMeshPartsSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateMeshImportCustomMeshSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateMeshImportMetaHumanMeshSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateMeshImportKeyPointsSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateMeshImportFacialTrackingSection()
				]
			];
	}

	if (ToolViewMainBox.IsValid())
	{
		ToolViewMainBox->AddSlot()
			.Padding(0.f, 4.f, 0.f, 0.f)
			.AutoHeight()
			[
				CreateMeshImportButtonSection()
			];
	}
}

void SMetaHumanCharacterEditorMeshImportToolView::NotifyPreChange(FProperty* PropertyAboutToChange)
{
	OnPreEditChangeProperty(PropertyAboutToChange, (PropertyAboutToChange) ? PropertyAboutToChange->GetName() : "");
}

void SMetaHumanCharacterEditorMeshImportToolView::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	const bool bIsInteractive = PropertyChangedEvent.ChangeType & EPropertyChangeType::Interactive;
	OnPostEditChangeProperty(PropertyThatChanged, bIsInteractive);
}

TSharedRef<SWidget> SMetaHumanCharacterEditorMeshImportToolView::CreateMeshImportToolbarSection()
{
	UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties());
	if (!MeshImportProperties)
	{
		return SNullWidget::NullWidget;
	}

	TSharedRef<SSegmentedControl<EMetaHumanMeshImportMode>> ModeSelectionWidget =
		SNew(SSegmentedControl<EMetaHumanMeshImportMode>)
		.Value(this, &SMetaHumanCharacterEditorMeshImportToolView::GetMeshImportMode)
		.OnValueChanged(this, &SMetaHumanCharacterEditorMeshImportToolView::OnMeshImportModeChanged);

	for (const EMetaHumanMeshImportMode Mode : TEnumRange<EMetaHumanMeshImportMode>())
	{
		FText SectionLabelText = FText::GetEmpty();
		switch (Mode)
		{
		default:
		case EMetaHumanMeshImportMode::Single:
			SectionLabelText = LOCTEXT("MeshImportMode_SingleMeshLabel", "Full Character");
			break;
		case EMetaHumanMeshImportMode::MeshParts:
			SectionLabelText = LOCTEXT("MeshImportMode_MeshPartsLabel", "Head and Body");
			break;
		}

		ModeSelectionWidget->AddSlot(Mode)
			.Text(SectionLabelText);
	}

	const TSharedRef<SWidget> ToolbarSectionWidget =
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.Padding(4.f)
		.AutoHeight()
		[
			ModeSelectionWidget
		];

	return ToolbarSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorMeshImportToolView::CreateMeshImportWarningTextSection()
{
	const TSharedRef<SWidget> WarningSectionWidget =
		SNew(SBox)
		.Padding(8.f)
		[
			SNew(STextBlock)
			.Text(this, &SMetaHumanCharacterEditorMeshImportToolView::GetWarningText)
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.AutoWrapText(true)
		];

	return WarningSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorMeshImportToolView::CreateMeshImportLoadCustomMeshSection()
{
	UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties());
	if (!MeshImportProperties)
	{
		return SNullWidget::NullWidget;
	}

	FProperty* CombinedMeshProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, CombinedMesh));
	
	const TSharedRef<SWidget> LoadCustomMeshSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("MeshImportLoadCustomMeshSectionLabel", "Load Custom Mesh"))
		.Visibility(this, &SMetaHumanCharacterEditorMeshImportToolView::GetMeshImportModeVisibility, EMetaHumanMeshImportMode::Single)
		.Content()
		[
			SNew(SVerticalBox)

			// Combined Mesh 
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyDefaultWidget(CombinedMeshProperty->GetDisplayNameText(), CombinedMeshProperty, MeshImportProperties, this)
			]
		];

	return LoadCustomMeshSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorMeshImportToolView::CreateMeshImportLoadMeshPartsSection()
{
	UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties());
	if (!MeshImportProperties)
	{
		return SNullWidget::NullWidget;
	}

	FProperty* BodyMeshProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, BodyMesh));
	FProperty* HeadMeshProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, HeadMesh));

	const TSharedRef<SWidget> LoadMeshPartsSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("MeshImportLoadMeshPartsSectionLabel", "Load Mesh Parts"))
		.Visibility(this, &SMetaHumanCharacterEditorMeshImportToolView::GetMeshImportModeVisibility, EMetaHumanMeshImportMode::MeshParts)
		.Content()
		[
			SNew(SVerticalBox)

			// Head Mesh
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyDefaultWidget(HeadMeshProperty->GetDisplayNameText(), HeadMeshProperty, MeshImportProperties, this)
			]

			// Body Mesh
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyDefaultWidget(BodyMeshProperty->GetDisplayNameText(), BodyMeshProperty, MeshImportProperties, this)
			]
		];

	return LoadMeshPartsSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorMeshImportToolView::CreateMeshImportCustomMeshSection()
{
	UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties());
	if (!MeshImportProperties)
	{
		return SNullWidget::NullWidget;
	}

	FProperty* UseGrayMaterialProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bUseGrayMaterialOnMesh));
	FProperty* MeshColorProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, MeshColor));
	FProperty* OpacityProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, MeshOpacity));
	FProperty* MeshOffsetProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, MeshOffset));

	const void* MeshImportPropertiesDefault = MeshImportProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorMeshImportToolProperties>();

	TSharedRef<SWidget> OpacityWidget = CreatePropertySpinBoxWidget(OpacityProperty->GetDisplayNameText(), OpacityProperty, MeshImportProperties, MeshImportPropertiesDefault);
	OpacityWidget->SetEnabled(TAttribute<bool>::CreateLambda([MeshImportProperties]()
    {
		return MeshImportProperties && MeshImportProperties->bUseGrayMaterialOnMesh;
	}));

	TSharedRef<SWidget> MeshColorWidget = CreatePropertyColorPickerWidget(MeshColorProperty->GetDisplayNameText(), MeshColorProperty, MeshImportProperties, MeshImportPropertiesDefault);
	MeshColorWidget->SetEnabled(TAttribute<bool>::CreateLambda([MeshImportProperties]()
	{
		return MeshImportProperties && MeshImportProperties->bUseGrayMaterialOnMesh;
	}));

	const TSharedRef<SWidget> CustomMeshSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("MeshImportCustomMeshSectionLabel", "Custom Mesh"))
		.Content()
		[
			SNew(SVerticalBox)

			// Use Gray Material
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyCheckBoxWidget(UseGrayMaterialProperty->GetDisplayNameText(), UseGrayMaterialProperty, MeshImportProperties, MeshImportPropertiesDefault)
			]

			// Mesh Color
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				MeshColorWidget
			]

			// Mesh Opacity
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				OpacityWidget
			]

			// Mesh Offset
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyDefaultWidget(MeshOffsetProperty->GetDisplayNameText(), MeshOffsetProperty, MeshImportProperties, this)
			]
		];

	return CustomMeshSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorMeshImportToolView::CreateMeshImportMetaHumanMeshSection()
{
	UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties());
	if (!MeshImportProperties)
	{
		return SNullWidget::NullWidget;
	}

	FProperty* ShowGuidesProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bUseGuidesTextureOnMetaHuman));
	FProperty* UseGrayMaterialProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bUseGrayMaterialOnMetaHuman));
	FProperty* MeshColorProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, MetaHumanMeshColor));
	FProperty* OpacityProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, MetaHumanOpacity));
	FProperty* OverlayProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, Overlay));

	const void* MeshImportPropertiesDefault = MeshImportProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorMeshImportToolProperties>();
	TSharedRef<SWidget> ShowGuidesWidget = CreatePropertyCheckBoxWidget(ShowGuidesProperty->GetDisplayNameText(), ShowGuidesProperty, MeshImportProperties, MeshImportPropertiesDefault);
	ShowGuidesWidget->SetEnabled(TAttribute<bool>::CreateLambda([MeshImportProperties]()
	{
		return MeshImportProperties && MeshImportProperties->bUseGrayMaterialOnMetaHuman;
	}));

	TSharedRef<SWidget> MetaHumanMeshColorWidget = CreatePropertyColorPickerWidget(MeshColorProperty->GetDisplayNameText(), MeshColorProperty, MeshImportProperties, MeshImportPropertiesDefault);
	MetaHumanMeshColorWidget->SetEnabled(TAttribute<bool>::CreateLambda([MeshImportProperties]()
	{
		return MeshImportProperties && MeshImportProperties->bUseGrayMaterialOnMetaHuman;
	}));

	TSharedRef<SWidget> MetaHumanOpacityWidget = CreatePropertySpinBoxWidget(OpacityProperty->GetDisplayNameText(), OpacityProperty, MeshImportProperties, MeshImportPropertiesDefault);
	MetaHumanOpacityWidget->SetEnabled(TAttribute<bool>::CreateLambda([MeshImportProperties]()
	{
		return MeshImportProperties && MeshImportProperties->bUseGrayMaterialOnMetaHuman;
	}));

	TSharedRef<SWidget> OverlayWidget = CreatePropertyComboBoxWidget<EMetaHumanCharacterOverlay>(OverlayProperty->GetDisplayNameText(), MeshImportProperties->Overlay, OverlayProperty, MeshImportProperties, MeshImportPropertiesDefault);
	OverlayWidget->SetEnabled(TAttribute<bool>::CreateLambda([MeshImportProperties]()
	{
		return MeshImportProperties && MeshImportProperties->bUseGrayMaterialOnMesh && MeshImportProperties->bUseGrayMaterialOnMetaHuman;
	}));

	const TSharedRef<SWidget> MetaHumanMeshSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("MeshImportMetaHumanMeshSectionLabel", "MetaHuman Mesh"))
		.Content()
		[
			SNew(SVerticalBox)

			// Use Gray Material
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyCheckBoxWidget(UseGrayMaterialProperty->GetDisplayNameText(), UseGrayMaterialProperty, MeshImportProperties, MeshImportPropertiesDefault)
			]

			// Mesh Color
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				MetaHumanMeshColorWidget
			]

			// Guides texture
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				ShowGuidesWidget
			]

			// MetaHuman Opacity
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				MetaHumanOpacityWidget
			]

			// Overlay
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				OverlayWidget
			]

		];

	return MetaHumanMeshSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorMeshImportToolView::CreateMeshImportKeyPointsSection()
{
	UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties());
	if (!MeshImportProperties)
	{
		return SNullWidget::NullWidget;
	}

	FProperty* KeyPointsScaleProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, KeyPointsScale));
	FProperty* MHKeyPointsColorProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, MHKeyPointsColor));
	FProperty* CustomKeyPointsColorProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, CustomKeyPointsColor));
	FProperty* ShowConnectionLinesProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bShowConnectionLines));
	FProperty* ShowPresetBodyKeyPointsProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bShowPresetKeyPoints));
	FProperty* ShowCustomKeyPointsProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bShowCustomKeyPoints));

	const void* MeshImportPropertiesDefault = MeshImportProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorMeshImportToolProperties>();

	const TSharedRef<SWidget> KeyPointsSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("MeshImportKeyPointsSectionLabel", "Key Points"))
		.Content()
		[
			SNew(SVerticalBox)

			// MetaHuman Key Points Color
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyColorPickerWidget(MHKeyPointsColorProperty->GetDisplayNameText(), MHKeyPointsColorProperty, MeshImportProperties, MeshImportPropertiesDefault)
			]

			// Custom Key Points Color
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyColorPickerWidget(CustomKeyPointsColorProperty->GetDisplayNameText(), CustomKeyPointsColorProperty, MeshImportProperties, MeshImportPropertiesDefault)
			]

			// Key Points Scale
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertySpinBoxWidget(KeyPointsScaleProperty->GetDisplayNameText(), KeyPointsScaleProperty, MeshImportProperties, MeshImportPropertiesDefault)
			]

			// Show Connection Lines
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyCheckBoxWidget(ShowConnectionLinesProperty->GetDisplayNameText(), ShowConnectionLinesProperty, MeshImportProperties, MeshImportPropertiesDefault)
			]

			// Show Preset Body Key Points
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyCheckBoxWidget(ShowPresetBodyKeyPointsProperty->GetDisplayNameText(), ShowPresetBodyKeyPointsProperty, MeshImportProperties, MeshImportPropertiesDefault)
			]
			
			// Show Custom Key Points
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyCheckBoxWidget(ShowCustomKeyPointsProperty->GetDisplayNameText(), ShowCustomKeyPointsProperty, MeshImportProperties, MeshImportPropertiesDefault)
			]

			// Delete Key Points button section
			+ SVerticalBox::Slot()
			.Padding(10.f)
			.AutoHeight()
			[
				CreateMeshImportToolButton
				(
					LOCTEXT("DeleteKeyPoints_Text", "Delete Key Points"),
					LOCTEXT("DeleteKeyPoints_Tooltip", "Removes all manually placed custom key points, resetting to only the preset key points."),
					FOnClicked::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::OnDeleteKeyPointsButtonClicked),
					TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::IsDeleteKeyPointsButtonEnabled),
					FAppStyle::GetBrush("Icons.Delete")
				)
			]
		];

	return KeyPointsSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorMeshImportToolView::CreateMeshImportFacialTrackingSection()
{
	UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties());
	if (!MeshImportProperties)
	{
		return SNullWidget::NullWidget;
	}

	FProperty* TrackingCurvesColorProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, TrackingCurvesColor));
	FProperty* TrackingPointsColorProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, TrackingPointsColor));
	FProperty* TrackingPointsSizeProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, TrackingPointsSize));

	const void* MeshImportPropertiesDefault = MeshImportProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorMeshImportToolProperties>();

	TSharedRef<SWidget> TrackingCurvesColorWidget = CreatePropertyColorPickerWidget(TrackingCurvesColorProperty->GetDisplayNameText(), TrackingCurvesColorProperty, MeshImportProperties, MeshImportPropertiesDefault);
	TrackingCurvesColorWidget->SetEnabled(TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::IsFacialCurvesEditingEnabled));

	TSharedRef<SWidget> TrackingPointsColorWidget = CreatePropertyColorPickerWidget(TrackingPointsColorProperty->GetDisplayNameText(), TrackingPointsColorProperty, MeshImportProperties, MeshImportPropertiesDefault);
	TrackingPointsColorWidget->SetEnabled(TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::IsFacialCurvesEditingEnabled));

	TSharedRef<SWidget> TrackingPointsSizeWidget = CreatePropertySpinBoxWidget(TrackingPointsSizeProperty->GetDisplayNameText(), TrackingPointsSizeProperty, MeshImportProperties, MeshImportPropertiesDefault);
	TrackingPointsSizeWidget->SetEnabled(TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::IsFacialCurvesEditingEnabled));

	const TSharedRef<SWidget> FacialTrackingSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("MeshImportFacialTrackingSectionLabel", "Facial Tracking"))
		.Content()
		[
			SNew(SVerticalBox)

			// Tracking Curves Color
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				TrackingCurvesColorWidget
			]

			// Tracking Points Color
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				TrackingPointsColorWidget
			]

			// Tracking Points Size
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				TrackingPointsSizeWidget
			]
		];

	return FacialTrackingSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorMeshImportToolView::CreateMeshImportButtonSection()
{
	UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties());
	if (!MeshImportProperties)
	{
		return SNullWidget::NullWidget;
	}

	FProperty* HeadDeltaProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, HeadDelta));
	FProperty* BodyDeltaProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, BodyDelta));

	const void* MeshImportPropertiesDefault = MeshImportProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorMeshImportToolProperties>();

	TSharedRef<SWidget> HeadDeltaWidget = CreatePropertySpinBoxWidget(HeadDeltaProperty->GetDisplayNameText(), HeadDeltaProperty, MeshImportProperties, MeshImportPropertiesDefault);
	HeadDeltaWidget->SetEnabled(TAttribute<bool>::CreateLambda([MeshImportProperties]() { return MeshImportProperties && MeshImportProperties->CanProcess(); }));

	TSharedRef<SWidget> BodyDeltaWidget = CreatePropertySpinBoxWidget(BodyDeltaProperty->GetDisplayNameText(), BodyDeltaProperty, MeshImportProperties, MeshImportPropertiesDefault);
	BodyDeltaWidget->SetEnabled(TAttribute<bool>::CreateLambda([MeshImportProperties]() { return MeshImportProperties && MeshImportProperties->CanProcess(); }));

	const TSharedRef<SWidget> ImportSectionWidget =
		SNew(SBorder)
		.Padding(-4.f, 4.f, -4.f, -4.f)
		.BorderImage(FMetaHumanCharacterEditorStyle::Get().GetBrush("MetaHumanCharacterEditorTools.ActiveToolLabel"))
		[
			SNew(SVerticalBox)

			// Manual Solve Action section
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SMetaHumanCharacterEditorToolPanel)
				.IsExpanded(false)
				.HierarchyLevel(EMetaHumanCharacterEditorPanelHierarchyLevel::Top)
				.RoundedBorders(false)
				.Label(LOCTEXT("MeshImportManualSolveActionsLabel", "Manual Solve Actions"))
				.Padding(12.f, 0.f)
				.Content()
				[
					SNew(SVerticalBox)

					// Refinement Delta parameters section
					+ SVerticalBox::Slot()
					.Padding(0.f, 2.f, 0.f, 0.f)
					.AutoHeight()
					[
						SNew(SMetaHumanCharacterEditorToolPanel)
						.Label(LOCTEXT("MeshImportRefinementDeltaSectionLabel", "Refinement Delta"))
						.HierarchyLevel(EMetaHumanCharacterEditorPanelHierarchyLevel::Middle)
						.RoundedBorders(false)
						.Content()
						[
							SNew(SVerticalBox)

							// Head Delta
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								HeadDeltaWidget
							]

							// Body Delta
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								BodyDeltaWidget
							]
						]
					]

					// Action buttons section
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SVerticalBox)

						// Trace Facial Features / Delete Face Curves button section
						+ SVerticalBox::Slot()
						.Padding(4.f)
						.AutoHeight()
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.Padding(0.f, 0.f, 2.f, 0.f)
							.FillWidth(1.f)
							[
								CreateMeshImportToolButton
								(
									LOCTEXT("TraceFacialFeaturesButton_Text", "Trace Facial Features"),
									LOCTEXT("TraceFacialFeaturesButton_Tooltip", "Analyzes the custom mesh head. Generates facial feature curves to improve the mesh fitting process."),
									FOnClicked::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::OnTraceFacialFeaturesButtonClicked),
									TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::IsTraceFacialFeaturesButtonEnabled)
								)
							]

							+ SHorizontalBox::Slot()
							.Padding(2.f, 0.f, 0.f, 0.f)
							.AutoWidth()
							[
								CreateMeshImportToolButton
								(
									FText::GetEmpty(),
									LOCTEXT("DeleteFaceCurvesButton_Tooltip", "Deletes all facial feature curves."),
									FOnClicked::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::OnDeleteFaceCurvesButtonClicked),
									TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::IsDeleteFaceCurvesButtonEnabled),
									FAppStyle::GetBrush("Icons.Delete")
								)
							]
						]

						// Solve buttons section
						+ SVerticalBox::Slot()
						.Padding(4.f)
						.AutoHeight()
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.Padding(2.f, 0.f, 0.f, 0.f)
							.FillWidth(.5f)
							[
								CreateMeshImportToolButton
								(
									LOCTEXT("SolveBodyButton_Text", "Solve Body"),
									LOCTEXT("SolveBodyButton_Tooltip", "Runs the solve pipeline on the body region only, leaving the head solve result unchanged."),
									FOnClicked::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::OnBodySolveStepButtonClicked),
									TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::IsBodySolveStepButtonEnabled)
								)
							]

							+ SHorizontalBox::Slot()
							.Padding(0.f, 0.f, 2.f, 0.f)
							.FillWidth(.5f)
							[
								CreateMeshImportToolButton
								(
									LOCTEXT("SolveHeadButton_Text", "Solve Head"),
									LOCTEXT("SolveHeadButton_Tooltip", "Runs the solve pipeline on the head region only, leaving the body solve result unchanged."),
									FOnClicked::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::OnFaceSolveStepButtonClicked),
									TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::IsFaceSolveStepButtonEnabled)
								)
							]
						]

						// Reset buttons section
						+ SVerticalBox::Slot()
						.Padding(4.f)
						.AutoHeight()
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.Padding(2.f, 0.f, 0.f, 0.f)
							.FillWidth(.5f)
							[
								CreateMeshImportToolButton
								(
									LOCTEXT("ResetBody_Text", "Reset Body"),
									LOCTEXT("ResetBody_Tooltip", "Discards the current body solve result and resets the body mesh back to the standard MetaHuman body shape."),
									FOnClicked::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::OnResetBodyButtonClicked),
									TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::IsResetBodyButtonEnabled),
									FAppStyle::GetBrush("PropertyWindow.DiffersFromDefault")
								)
							]

							+ SHorizontalBox::Slot()
							.Padding(0.f, 0.f, 2.f, 0.f)
							.FillWidth(.5f)
							[
								CreateMeshImportToolButton
								(
									LOCTEXT("ResetHead_Text", "Reset Head"),
									LOCTEXT("ResetHead_Tooltip", "Discards the current head solve result and resets the head mesh back to the standard MetaHuman head shape."),
									FOnClicked::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::OnResetHeadButtonClicked),
									TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::IsResetHeadButtonEnabled),
									FAppStyle::GetBrush("PropertyWindow.DiffersFromDefault")
								)
							]
						]

						// Refine Vertices button section
						+ SVerticalBox::Slot()
						.Padding(4.f)
						.AutoHeight()
						[
							CreateMeshImportToolButton
							(
								LOCTEXT("RefineVerticesButtonText", "Refine Vertices"),
								LOCTEXT("RefineVerticesButtonTooltip", "Applies an additional vertex-level refinement pass on top of the current solve result."),
								FOnClicked::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::OnRefineVerticesButtonClicked),
								TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::IsRefineVerticesButtonEnabled)
							)
						]

						// Save Pose button section
						+ SVerticalBox::Slot()
						.Padding(4.f)
						.AutoHeight()
						[
							CreateMeshImportToolButton
							(
								LOCTEXT("SavePoseButton_Text", "Save Pose"),
								LOCTEXT("SavePoseButton_Tooltip", "Exports the current posed MetaHuman as a DNA asset."),
								FOnClicked::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::OnSavePoseButtonClicked),
								TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::IsSavePoseButtonEnabled),
								FAppStyle::GetBrush("Icons.Export")
							)
						]

						// Advanced Features button section
						+ SVerticalBox::Slot()
						.Padding(4.f)
						.AutoHeight()
						[
							CreateMeshImportToolButton
							(
								LOCTEXT("AdvancedFeaturesButton_Text", "Advanced Features"),
								LOCTEXT("AdvancedFeaturesButton_Tooltip", "Opens a window with advanced features to further customize the process."),
								FOnClicked::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::OnAdvancedFeaturesButtonClicked),
								/* bIsEnabled*/ true
							)
						]
					]
				]
			]

			// Import button section
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(50.f)
				.HAlign(HAlign_Fill)
				.Padding(10.f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
					.ToolTipText(LOCTEXT("ImportButton_Tooltip", "Runs the full automated solve pipeline using the current imported custom mesh as input."))
					.ForegroundColor(FLinearColor::White)
					.IsEnabled(this, &SMetaHumanCharacterEditorMeshImportToolView::IsImportButtonEnabled)
					.OnClicked(this, &SMetaHumanCharacterEditorMeshImportToolView::OnImportButtonClicked)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ImportButtonText", "Auto Solve"))
						.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					]
				]
			]
		];

	return ImportSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorMeshImportToolView::CreateAdvancedPropertiesWindow()
{
	UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties());
	if (!MeshImportProperties)
	{
		return SNullWidget::NullWidget;
	}

	FProperty* ShowBodyModelMeshProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bShowBodyModelMesh));
	FProperty* ShowBodyModelAPoseProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bShowBodyModelAPose));
	FProperty* ShowMetaHumanAPoseProperty = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bShowMetaHumanAPose));

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

	const TSharedRef<FStructOnScope> ConformSolveSettingsStructOnScope = MakeShared<FStructOnScope>(FBodyConformSolveSettings::StaticStruct(), (uint8*)&MeshImportProperties->BodySolveSettings);
	const TSharedRef<FStructOnScope> RefinementSettingsStructOnScope = MakeShared<FStructOnScope>(FRefinementSettings::StaticStruct(), (uint8*)&MeshImportProperties->RefinementSettings);

	const TSharedRef<IStructureDetailsView> ConformSolveSettingsDetailsView = PropertyEditorModule.CreateStructureDetailView(DetailsViewArgs, StructureViewArgs, ConformSolveSettingsStructOnScope);
	const TSharedRef<IStructureDetailsView> RefinementSettingsDetailsView = PropertyEditorModule.CreateStructureDetailView(DetailsViewArgs, StructureViewArgs, RefinementSettingsStructOnScope);

	const void* MeshImportPropertiesDefault = MeshImportProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorMeshImportToolProperties>();

	const TSharedRef<SWidget> AdvancedSectionWidget =

		SNew(SScrollBox)

		+ SScrollBox::Slot()
		.AutoSize()
		[
			SNew(SMetaHumanCharacterEditorToolPanel)
			.Label(LOCTEXT("MeshImportAdvancedSectionLabel", "Advanced"))
			.RoundedBorders(false)
			.Content()
			[
				SNew(SVerticalBox)

				// Show Body Model Mesh
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					CreatePropertyCheckBoxWidget(ShowBodyModelMeshProperty->GetDisplayNameText(), ShowBodyModelMeshProperty, MeshImportProperties, MeshImportPropertiesDefault)
				]

				// Show Body Model A-Pose
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					CreatePropertyCheckBoxWidget(ShowBodyModelAPoseProperty->GetDisplayNameText(), ShowBodyModelAPoseProperty, MeshImportProperties, MeshImportPropertiesDefault)
				]

				// Show MetaHuman A-Pose
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					CreatePropertyCheckBoxWidget(ShowMetaHumanAPoseProperty->GetDisplayNameText(), ShowMetaHumanAPoseProperty, MeshImportProperties, MeshImportPropertiesDefault)
				]

				// Align Head button
				+ SVerticalBox::Slot()
				.Padding(4.f, 8.f)
				.AutoHeight()
				[
					CreateMeshImportToolButton
					(
						LOCTEXT("AlignHeadButton_Text", "Align Head To Facial Tracking"),
						LOCTEXT("AlignHeadButton_Tooltip", "Aligns the head of the imported mesh to better match the facial tracking data."),
						FOnClicked::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::OnAlignHeadButtonClicked),
						TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::IsAlignHeadButtonEnabled)
					)
				]

				// Body Solve Settings
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					ConformSolveSettingsDetailsView->GetWidget().ToSharedRef()
				]

				// Refinement Settings 
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					RefinementSettingsDetailsView->GetWidget().ToSharedRef()
				]
			]
		];

	return AdvancedSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorMeshImportToolView::CreateMeshImportToolButton(const FText& ButtonText, const FText& ToolTipText, const FOnClicked& OnClickedHandler, TAttribute<bool> IsEnabled, const FSlateBrush* Icon) const
{

	TSharedPtr<SWidget> IconWidget = SNullWidget::NullWidget;
	if(Icon)
	{
		IconWidget = SNew(SImage)
			.Image(Icon)
			.DesiredSizeOverride(FVector2D(16.f, 8.f))
			.ColorAndOpacity(FStyleColors::White);
	}

	return
		SNew(SBox)
		.HeightOverride(26.f)
		.HAlign(HAlign_Fill)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "Button")
			.ForegroundColor(FLinearColor::White)
			.ToolTipText(ToolTipText)
			.IsEnabled(IsEnabled)
			.OnClicked(OnClickedHandler)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.Padding(2.f, 0.f)
				.AutoWidth()
				[
					IconWidget.ToSharedRef()
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 3.f, 2.f, 0.f)
				[
					SNew(STextBlock)
					.Text(ButtonText)
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				]
			]
		];
}

void SMetaHumanCharacterEditorMeshImportToolView::OnAdvancedFeaturesWindowClosed(const TSharedRef<SWindow>& Window)
{
	AdvancedFeaturesWindow = nullptr;
}

EMetaHumanMeshImportMode SMetaHumanCharacterEditorMeshImportToolView::GetMeshImportMode() const
{
	if (const UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		return MeshImportProperties->Mode;
	}

	return EMetaHumanMeshImportMode::Single;
}

void SMetaHumanCharacterEditorMeshImportToolView::OnMeshImportModeChanged(EMetaHumanMeshImportMode Mode)
{
	if (UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		MeshImportProperties->Mode = Mode;
		MeshImportProperties->bUseCharacterParts = Mode == EMetaHumanMeshImportMode::MeshParts;
		FProperty* Property = FindFProperty<FProperty>(UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass(), GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bUseCharacterParts));
		OnPostEditChangeProperty(Property, /*bIsInteractive*/false);
	}
}

FReply SMetaHumanCharacterEditorMeshImportToolView::OnResetBodyButtonClicked()
{
	if (UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		MeshImportProperties->ResetBody();
	}

	return FReply::Handled();
}

FReply SMetaHumanCharacterEditorMeshImportToolView::OnResetHeadButtonClicked()
{
	if (UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		MeshImportProperties->ResetHead();
	}

	return FReply::Handled();
}


FReply SMetaHumanCharacterEditorMeshImportToolView::OnSavePoseButtonClicked()
{
	if (UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		CastChecked<UMetaHumanCharacterEditorMeshImportTool>(GetTool())->ExportStateToDNA();
	}

	return FReply::Handled();
}

FReply SMetaHumanCharacterEditorMeshImportToolView::OnDeleteKeyPointsButtonClicked()
{
	if (UMetaHumanCharacterEditorMeshImportTool* MeshImportTool = Cast<UMetaHumanCharacterEditorMeshImportTool>(Tool.Get()))
	{
		MeshImportTool->RemoveAllKeyPoints();
	}

	return FReply::Handled();
}

bool SMetaHumanCharacterEditorMeshImportToolView::IsResetBodyButtonEnabled() const
{
	if (const UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		return MeshImportProperties->CanReset();
	}

	return false;
}

bool SMetaHumanCharacterEditorMeshImportToolView::IsResetHeadButtonEnabled() const
{
	if (const UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		return MeshImportProperties->CanReset();
	}

	return false;
}

bool SMetaHumanCharacterEditorMeshImportToolView::IsSavePoseButtonEnabled() const
{
	if (const UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		return MeshImportProperties->CanProcess();
	}

	return false;
}

bool SMetaHumanCharacterEditorMeshImportToolView::IsDeleteKeyPointsButtonEnabled() const
{
	if (const UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		return MeshImportProperties->CanProcess();
	}

	return false;
}

bool SMetaHumanCharacterEditorMeshImportToolView::IsTraceFacialFeaturesButtonEnabled() const
{
	if (const UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		return MeshImportProperties->CanTrackFace();
	}

	return false;
}

bool SMetaHumanCharacterEditorMeshImportToolView::IsFacialCurvesEditingEnabled() const
{
	if (const UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		return MeshImportProperties->bShowFacialTracking;
	}

	return false;
}

FReply SMetaHumanCharacterEditorMeshImportToolView::OnTraceFacialFeaturesButtonClicked()
{
	if (UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		MeshImportProperties->TrackFace();
	}

	return FReply::Handled();
}

bool SMetaHumanCharacterEditorMeshImportToolView::IsDeleteFaceCurvesButtonEnabled() const
{
	if (const UMetaHumanCharacterEditorMeshImportTool* MeshImportTool = Cast<UMetaHumanCharacterEditorMeshImportTool>(Tool.Get()))
	{
		if (MeshImportTool->GetMeshImportProperties())
		{
			return MeshImportTool->HasFaceCurves() && MeshImportTool->GetMeshImportProperties()->CanProcess();
		}
	}

	return false;
}

FReply SMetaHumanCharacterEditorMeshImportToolView::OnDeleteFaceCurvesButtonClicked()
{
	if (UMetaHumanCharacterEditorMeshImportTool* MeshImportTool = Cast<UMetaHumanCharacterEditorMeshImportTool>(Tool.Get()))
	{
		MeshImportTool->DeleteFaceCurves();
	}

	return FReply::Handled();
}

bool SMetaHumanCharacterEditorMeshImportToolView::IsAlignHeadButtonEnabled() const
{
	if (const UMetaHumanCharacterEditorMeshImportTool* MeshImportTool = Cast<UMetaHumanCharacterEditorMeshImportTool>(Tool.Get()))
	{
		if (MeshImportTool->GetMeshImportProperties())
		{
			return MeshImportTool->HasFaceCurves() && MeshImportTool->GetMeshImportProperties()->CanProcess();
		}
	}

	return false;
}

FReply SMetaHumanCharacterEditorMeshImportToolView::OnAlignHeadButtonClicked()
{
	if (UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		MeshImportProperties->StartAlignHeadMeshConform();
	}

	return FReply::Handled();
}

bool SMetaHumanCharacterEditorMeshImportToolView::IsBodySolveStepButtonEnabled() const
{
	if (UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		return MeshImportProperties->CanProcess();
	}

	return false;
}

FReply SMetaHumanCharacterEditorMeshImportToolView::OnBodySolveStepButtonClicked()
{
	if (UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		MeshImportProperties->StartBodyStepMeshConform();
	}

	return FReply::Handled();
}

bool SMetaHumanCharacterEditorMeshImportToolView::IsFaceSolveStepButtonEnabled() const
{
	if (UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		return MeshImportProperties->CanProcess();
	}

	return false;
}

FReply SMetaHumanCharacterEditorMeshImportToolView::OnFaceSolveStepButtonClicked()
{
	if (UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		MeshImportProperties->StartFaceStepMeshConform();
	}

	return FReply::Handled();
}

bool SMetaHumanCharacterEditorMeshImportToolView::IsRefineVerticesButtonEnabled() const
{
	if (const UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		return MeshImportProperties->CanProcess();
	}

	return false;
}

FReply SMetaHumanCharacterEditorMeshImportToolView::OnRefineVerticesButtonClicked()
{
	if (UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		MeshImportProperties->RefineVertices();
	}

	return FReply::Handled();
}

bool SMetaHumanCharacterEditorMeshImportToolView::IsImportButtonEnabled() const
{
	if (UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		return MeshImportProperties->CanProcess();
	}

	return false;
}

FReply SMetaHumanCharacterEditorMeshImportToolView::OnAdvancedFeaturesButtonClicked()
{
	if (AdvancedFeaturesWindow.IsValid())
	{
		AdvancedFeaturesWindow->BringToFront();
		return FReply::Handled();
	}

	const FText TitleText = LOCTEXT("AdvancedFeaturesWindow", "Advanced Features");
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(TitleText)
		.HasCloseButton(true)
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(400.0f, 600.0f))
		.AutoCenter(EAutoCenter::PreferredWorkArea)
		.SupportsMinimize(true)
		.SupportsMaximize(true)
		.IsTopmostWindow(true)
		[
			CreateAdvancedPropertiesWindow()
		];

	Window->SetOnWindowClosed(FOnWindowClosed::CreateSP(this, &SMetaHumanCharacterEditorMeshImportToolView::OnAdvancedFeaturesWindowClosed));

	AdvancedFeaturesWindow = Window;
	FSlateApplication::Get().AddWindow(Window);

	return FReply::Handled();
}

FReply SMetaHumanCharacterEditorMeshImportToolView::OnImportButtonClicked()
{
	if (UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties()))
	{
		MeshImportProperties->StartAutoMeshConform();
	}

	return FReply::Handled();
}

FText SMetaHumanCharacterEditorMeshImportToolView::GetWarningText() const
{
	const UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties());
	if (MeshImportProperties)
	{
		switch (MeshImportProperties->Mode)
		{
		case EMetaHumanMeshImportMode::Single:
			return LOCTEXT("MeshImportWarningText_SingleMesh", "Use a full body custom mesh to get an automatically rigged MetaHuman based on the closest match. Use this for creating a character from a scan or sculpt with a custom mesh that doesn’t match MetaHuman's topology.");
		case EMetaHumanMeshImportMode::MeshParts:
			return LOCTEXT("MeshImportWarningText_MeshParts", "Use a head and/or body custom mesh to get an automatically rigged MetaHuman based on the closest match. Use this for creating a character  from scans or sculpts with custom meshes that don’t match MetaHuman's topology.");
		}
	}

	return FText::GetEmpty();
}

EVisibility SMetaHumanCharacterEditorMeshImportToolView::GetMeshImportModeVisibility(EMetaHumanMeshImportMode Mode) const
{
	const UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = Cast<UMetaHumanCharacterEditorMeshImportToolProperties>(GetToolProperties());
	const bool bIsVisible = MeshImportProperties && MeshImportProperties->Mode == Mode;
	return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
