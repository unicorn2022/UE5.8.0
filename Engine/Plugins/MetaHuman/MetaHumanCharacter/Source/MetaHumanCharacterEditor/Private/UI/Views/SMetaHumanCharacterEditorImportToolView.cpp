// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditorImportToolView.h"

#include "Algo/Find.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "IDetailsView.h"
#include "MetaHumanCharacterEditorStyle.h"
#include "Tools/MetaHumanCharacterEditorImportTools.h"
#include "UI/Widgets/SMetaHumanCharacterEditorTileView.h"
#include "UI/Widgets/SMetaHumanCharacterEditorToolPanel.h"
#include "InteractiveToolManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCharacterEditorConformToolView"

TSharedRef<SWidget> SMetaHumanCharacterEditorImportToolView::CreateImportToolViewWarningSection()
{
	const TSharedRef<SWidget> WarningSectionWidget =
		SNew(SBox)
		.Padding(8.f)
		[
			SNew(STextBlock)
			.Text(this, &SMetaHumanCharacterEditorImportToolView::GetWarningText)
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.AutoWrapText(true)
			.Visibility(this, &SMetaHumanCharacterEditorImportToolView::GetWarningVisibility)
		];

	return WarningSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorImportToolView::CreateImportToolViewButtonSection()
{
	const TSharedRef<SWidget> ImportSectionWidget =
		SNew(SBorder)
		.Padding(-4.f)
		.BorderImage(FMetaHumanCharacterEditorStyle::Get().GetBrush("MetaHumanCharacterEditorTools.ActiveToolLabel"))
		[
			SNew(SVerticalBox)

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
					.ForegroundColor(FLinearColor::White)
					.IsEnabled(this, &SMetaHumanCharacterEditorImportToolView::IsImportButtonEnabled)
					.OnClicked(this, &SMetaHumanCharacterEditorImportToolView::OnImportButtonClicked)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SMetaHumanCharacterEditorImportToolView::GetImportButtonText)
						.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					]
				]
			]
		];

	return ImportSectionWidget;
}

FText SMetaHumanCharacterEditorImportToolView::GetImportButtonText() const
{
	return LOCTEXT("ImportButtonText", "Apply");
}

FText SMetaHumanCharacterEditorImportToolView::GetWarningText() const
{
	return FText::GetEmpty();
}

EVisibility SMetaHumanCharacterEditorImportToolView::GetWarningVisibility() const
{
	return GetWarningText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

void SMetaHumanCharacterEditorInternalImportToolView::Construct(const FArguments& InArgs, UMetaHumanCharacterEditorImportTool* InTool)
{
	SMetaHumanCharacterEditorToolView::Construct(SMetaHumanCharacterEditorToolView::FArguments(), InTool);
}

UInteractiveToolPropertySet* SMetaHumanCharacterEditorInternalImportToolView::GetToolProperties() const
{
	const UMetaHumanCharacterEditorImportTool* ImportTool = Cast<UMetaHumanCharacterEditorImportTool>(Tool);
	return IsValid(ImportTool) ? ImportTool->GetImportToolProperties() : nullptr;
}

void SMetaHumanCharacterEditorInternalImportToolView::MakeToolView()
{
	if(ToolViewScrollBox.IsValid())
	{
		ToolViewScrollBox->AddSlot()
			.VAlign(VAlign_Top)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateImportToolViewWarningSection()
				]
			];
	}

	if (ToolViewMainBox.IsValid())
	{
		ToolViewMainBox->AddSlot()
			.Padding(0.f, 4.f, 0.f, 0.f)
			.AutoHeight()
			[
				CreateImportToolViewButtonSection()
			];
	}
}

void SMetaHumanCharacterEditorInternalImportToolView::NotifyPreChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange)
	{
		OnPreEditChangeProperty(PropertyAboutToChange, PropertyAboutToChange->GetName());
	}
}

void SMetaHumanCharacterEditorInternalImportToolView::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	if (PropertyThatChanged)
	{
		const bool bIsInteractive = PropertyChangedEvent.ChangeType & EPropertyChangeType::Interactive;
		OnPostEditChangeProperty(PropertyThatChanged, bIsInteractive);
	}
}

TSharedRef<SWidget> SMetaHumanCharacterEditorInternalImportToolView::CreateImportToolToolbarSection()
{
	UMetaHumanCharacterImportSubToolBase* ImportToolProperties = Cast<UMetaHumanCharacterImportSubToolBase>(GetToolProperties());
	if (!ImportToolProperties)
	{
		return SNullWidget::NullWidget;
	}

	TSharedRef<SSegmentedControl<EMetaHumanImportToolMode>> ModeSelectionWidget =
		SNew(SSegmentedControl<EMetaHumanImportToolMode>)
		.Value(this, &SMetaHumanCharacterEditorInternalImportToolView::GetImportToolMode)
		.OnValueChanged(this, &SMetaHumanCharacterEditorInternalImportToolView::OnImportToolModeChanged);

	for (const EMetaHumanImportToolMode Mode : TEnumRange<EMetaHumanImportToolMode>())
	{
		FText SectionLabelText = FText::GetEmpty();
		switch (Mode)
		{
		default:
		case EMetaHumanImportToolMode::MeshFit:
			SectionLabelText = LOCTEXT("ImportToolMode_MeshFitLabel", "Mesh Fit");
			break;
		case EMetaHumanImportToolMode::Replace:
			SectionLabelText = LOCTEXT("ImportToolMode_ReplaceLabel", "Replace");
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

EMetaHumanImportToolMode SMetaHumanCharacterEditorInternalImportToolView::GetImportToolMode() const
{
	if (const UMetaHumanCharacterImportSubToolBase* ImportToolProperties = Cast<UMetaHumanCharacterImportSubToolBase>(GetToolProperties()))
	{
		return ImportToolProperties->Mode;
	}

	return EMetaHumanImportToolMode::MeshFit;
}

void SMetaHumanCharacterEditorInternalImportToolView::OnImportToolModeChanged(EMetaHumanImportToolMode Mode)
{
	if (UMetaHumanCharacterImportSubToolBase* ImportToolProperties = Cast<UMetaHumanCharacterImportSubToolBase>(GetToolProperties()))
	{
		ImportToolProperties->Mode = Mode;
	}
}

bool SMetaHumanCharacterEditorInternalImportToolView::IsImportButtonEnabled() const
{
	UMetaHumanCharacterImportSubToolBase* ImportToolProperties = Cast<UMetaHumanCharacterImportSubToolBase>(GetToolProperties());
	return ImportToolProperties && ImportToolProperties->CanImportMesh();
}

FReply SMetaHumanCharacterEditorInternalImportToolView::OnImportButtonClicked()
{
	UMetaHumanCharacterImportSubToolBase* ImportToolProperties = Cast<UMetaHumanCharacterImportSubToolBase>(GetToolProperties());
	if (IsValid(ImportToolProperties))
	{
		ImportToolProperties->ImportMesh();
	}

	return FReply::Handled();
}

FText SMetaHumanCharacterEditorInternalImportToolView::GetWarningText() const
{
	return LOCTEXT("HeadImportWarning", "Conforming will reposition Head joints and vertices to best align Head and Body.\n\nThe originating File or Asset won’t be modified.");
}

EVisibility SMetaHumanCharacterEditorInternalImportToolView::GetImportToolModeVisibility(EMetaHumanImportToolMode Mode) const
{
	const UMetaHumanCharacterImportSubToolBase* ImportToolProperties = Cast<UMetaHumanCharacterImportSubToolBase>(GetToolProperties());
	const bool bIsVisible = ImportToolProperties && ImportToolProperties->Mode == Mode;
	return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

void SMetaHumanCharacterEditorImportFromDNAToolView::Construct(const FArguments& InArgs, UMetaHumanCharacterEditorImportTool* InTool)
{
	SMetaHumanCharacterEditorInternalImportToolView::Construct(SMetaHumanCharacterEditorInternalImportToolView::FArguments(), InTool);
}

void SMetaHumanCharacterEditorImportFromDNAToolView::MakeToolView()
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
					CreateImportToolToolbarSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateImportToolViewWarningSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateImportLoadDNAAssetsSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateImportBodyMeshSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateImportBodyJointsSection()
				]
			];
	}

	if (ToolViewMainBox.IsValid())
	{
		ToolViewMainBox->AddSlot()
			.Padding(0.f, 4.f, 0.f, 0.f)
			.AutoHeight()
			[
				CreateImportToolViewButtonSection()
			];
	}
}

bool SMetaHumanCharacterEditorImportFromDNAToolView::IsImportButtonEnabled() const
{
	const UMetaHumanCharacterImportFromDNAProperties* ImportFromDNAProperties = Cast<UMetaHumanCharacterImportFromDNAProperties>(GetToolProperties());
	if (ImportFromDNAProperties)
	{
		switch (ImportFromDNAProperties->Mode)
		{
		case EMetaHumanImportToolMode::MeshFit:
			return ImportFromDNAProperties->CanConform();
		case EMetaHumanImportToolMode::Replace:
			return ImportFromDNAProperties->CanImportMesh();
		}
	}

	return false;
}

FReply SMetaHumanCharacterEditorImportFromDNAToolView::OnImportButtonClicked()
{
	UMetaHumanCharacterImportFromDNAProperties* ImportFromDNAProperties = Cast<UMetaHumanCharacterImportFromDNAProperties>(GetToolProperties());
	if (ImportFromDNAProperties)
	{
		switch (ImportFromDNAProperties->Mode)
		{
		case EMetaHumanImportToolMode::MeshFit:
			ImportFromDNAProperties->Conform();
			break;
		case EMetaHumanImportToolMode::Replace:
			if (ImportFromDNAProperties->ImportOptions.bImportWholeRig)
			{
				ImportFromDNAProperties->ImportWholeRig();
			}
			else
			{
				if (ImportFromDNAProperties->bReplaceMesh)
				{
					ImportFromDNAProperties->ImportMesh();
				}
				if (ImportFromDNAProperties->bReplaceBodyJoints)
				{
					ImportFromDNAProperties->ImportJoints();
				}
			}
			break;
		}
	}

	return FReply::Handled();
}

FText SMetaHumanCharacterEditorImportFromDNAToolView::GetWarningText() const
{
	const UMetaHumanCharacterImportSubToolBase* ImportToolProperties = Cast<UMetaHumanCharacterImportSubToolBase>(GetToolProperties());
	if (ImportToolProperties)
	{
		switch (ImportToolProperties->Mode)
		{
		case EMetaHumanImportToolMode::MeshFit:
			return LOCTEXT("DNAImportMeshFitWarningText", "Use DNA files to load an exact MetaHuman face and/or body with all its custom rig work intact. Use this to reuse characters from other projects or preserve modifications made in external tools.");
		case EMetaHumanImportToolMode::Replace:
			return LOCTEXT("DNAImportReplaceWarningText", "Import custom meshes and/or joint hierarchies to replace the existing MetaHuman face or body. Use this to bring in externally modified geometry or rigs while preserving compatibility with the MetaHuman system.");
		}
	}

	return FText::GetEmpty();
}

TSharedRef<SWidget> SMetaHumanCharacterEditorImportFromDNAToolView::CreateImportLoadDNAAssetsSection()
{
	UMetaHumanCharacterImportFromDNAProperties* ImportFromDNAProperties = Cast<UMetaHumanCharacterImportFromDNAProperties>(GetToolProperties());
	FConformBodyParams* BodyParams = IsValid(ImportFromDNAProperties) ? &ImportFromDNAProperties->BodyParams : nullptr;
	FImportFromDNAParams* ImportFromDNAParams = IsValid(ImportFromDNAProperties) ? &ImportFromDNAProperties->ImportOptions : nullptr;
	if (!BodyParams || !ImportFromDNAParams)
	{
		return SNullWidget::NullWidget;
	}

	UStruct* BodyStruct = FConformBodyParams::StaticStruct();
	UStruct* ImportStruct = FImportFromDNAParams::StaticStruct();

	FProperty* DNAHeadProperty = UMetaHumanCharacterImportFromDNAProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromDNAProperties, DNAHead));
	FProperty* DNABodyProperty = UMetaHumanCharacterImportFromDNAProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromDNAProperties, DNABody));
	FProperty* AlignScaleProperty = UMetaHumanCharacterImportFromDNAProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromDNAProperties, bAlignScale));
	FProperty* AlignRotationProperty = UMetaHumanCharacterImportFromDNAProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromDNAProperties, bAlignRotation));
	FProperty* AlignTranslationProperty = UMetaHumanCharacterImportFromDNAProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromDNAProperties, bAlignTranslation));
	FProperty* ImportWholeRigProperty = ImportStruct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FImportFromDNAParams, bImportWholeRig));
	FProperty* EstimateJointsFromMeshProperty = BodyStruct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FConformBodyParams, bEstimateJointsFromMesh));
	FProperty* IsolateHeadFromBodyProperty = ImportStruct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FImportFromDNAParams, bIsolateHeadFromBody));

	const void* ImportFromDNAPropertiesDefault = ImportFromDNAProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterImportFromDNAProperties>();
	const void* BodyParamsDefault = &ImportFromDNAProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterImportFromDNAProperties>()->BodyParams;
	const void* ImportOptionsDefault = &ImportFromDNAProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterImportFromDNAProperties>()->ImportOptions;

	TSharedRef<SWidget> DNABodyWidget = CreatePropertyDefaultWidget(DNABodyProperty->GetDisplayNameText(), DNABodyProperty, ImportFromDNAProperties, this);
	DNABodyWidget->SetEnabled(TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorImportFromDNAToolView::IsDNABodyEnabled));

	TSharedRef<SWidget> ImportWholeRigWidget = CreatePropertyCheckBoxWidget(ImportWholeRigProperty->GetDisplayNameText(), ImportWholeRigProperty, ImportFromDNAParams, ImportOptionsDefault);
	ImportWholeRigWidget->SetEnabled(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateUObject(ImportFromDNAProperties, &UMetaHumanCharacterImportFromDNAProperties::CanImportWholeRig)));
	ImportWholeRigWidget->SetVisibility(TAttribute<EVisibility>::CreateSP(this, &SMetaHumanCharacterEditorImportFromDNAToolView::GetImportToolModeVisibility, EMetaHumanImportToolMode::Replace));

	const TSharedRef<SWidget> LoadDNAAssetsSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("LoadDNAAssetsSectionLabel", "Files"))
		.Content()
		[
			SNew(SVerticalBox)

			// DNA Head
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyDefaultWidget(DNAHeadProperty->GetDisplayNameText(), DNAHeadProperty, ImportFromDNAProperties, this)
			]

			// DNA Body
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				DNABodyWidget
			]

			// Import Whole Rig
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				ImportWholeRigWidget
			]

			// Head Options section
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SMetaHumanCharacterEditorToolPanel)
				.Label(LOCTEXT("ImportFromDNAHeadAlignSectionLabel", "Head Options"))
				.Visibility(this, &SMetaHumanCharacterEditorImportFromDNAToolView::GetImportToolModeVisibility, EMetaHumanImportToolMode::MeshFit)
				.RoundedBorders(false)
				.HierarchyLevel(EMetaHumanCharacterEditorPanelHierarchyLevel::Middle)
				.Content()
				[
					SNew(SVerticalBox)

					// Align Scale
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						CreatePropertyCheckBoxWidget(AlignScaleProperty->GetDisplayNameText(), AlignScaleProperty, ImportFromDNAProperties, ImportFromDNAPropertiesDefault)
					]

					// Align Rotation
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						CreatePropertyCheckBoxWidget(AlignRotationProperty->GetDisplayNameText(), AlignRotationProperty, ImportFromDNAProperties, ImportFromDNAPropertiesDefault)
					]

					// Align Translation
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						CreatePropertyCheckBoxWidget(AlignTranslationProperty->GetDisplayNameText(), AlignTranslationProperty, ImportFromDNAProperties, ImportFromDNAPropertiesDefault)
					]
				]
			]

			// Advanced section
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SMetaHumanCharacterEditorToolPanel)
				.Label(LOCTEXT("ImportFromDNAAdvancedSectionLabel", "Advanced"))
				.Visibility(this, &SMetaHumanCharacterEditorImportFromDNAToolView::GetImportToolModeVisibility, EMetaHumanImportToolMode::MeshFit)
				.RoundedBorders(false)
				.HierarchyLevel(EMetaHumanCharacterEditorPanelHierarchyLevel::Middle)
				.Content()
				[
					SNew(SVerticalBox)

					// Estimate Joints from Mesh
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						CreatePropertyCheckBoxWidget(EstimateJointsFromMeshProperty->GetDisplayNameText(), EstimateJointsFromMeshProperty, BodyParams, BodyParamsDefault)
					]

					// Adopt Custom Neck
					+ SVerticalBox::Slot()
					.AutoHeight()
					[	
						CreatePropertyCheckBoxWidget(IsolateHeadFromBodyProperty->GetDisplayNameText(), IsolateHeadFromBodyProperty, ImportFromDNAParams, ImportOptionsDefault)
					]
				]
			]
		];

	return LoadDNAAssetsSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorImportFromDNAToolView::CreateImportBodyMeshSection()
{
	UMetaHumanCharacterImportFromDNAProperties* ImportFromDNAProperties = Cast<UMetaHumanCharacterImportFromDNAProperties>(GetToolProperties());
	FConformBodyParams* BodyParams = IsValid(ImportFromDNAProperties) ? &ImportFromDNAProperties->BodyParams : nullptr;
	if (!BodyParams)
	{
		return SNullWidget::NullWidget;
	}

	UStruct* Struct = FConformBodyParams::StaticStruct();

	FProperty* ReplaceMeshProperty = UMetaHumanCharacterImportFromDNAProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromDNAProperties, bReplaceMesh));
	FProperty* AutoRigHelperJointsProperty = Struct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FConformBodyParams, bAutoRigHelperJoints));

	const void* ImportFromDNAPropertiesDefault = ImportFromDNAProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterImportFromDNAProperties>();
	const void* BodyParamsDefault = &ImportFromDNAProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterImportFromDNAProperties>()->BodyParams;

	TSharedRef<SWidget> ReplaceMeshWidget = CreatePropertyCheckBoxWidget(ReplaceMeshProperty->GetDisplayNameText(), ReplaceMeshProperty, ImportFromDNAProperties, ImportFromDNAPropertiesDefault);
	ReplaceMeshWidget->SetEnabled(TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorImportFromDNAToolView::IsReplaceMeshEnabled));

	TSharedRef<SWidget> AutoRigHelperJointsWidget = CreatePropertyCheckBoxWidget(AutoRigHelperJointsProperty->GetDisplayNameText(), AutoRigHelperJointsProperty, BodyParams, BodyParamsDefault);
	AutoRigHelperJointsWidget->SetEnabled(TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorImportFromDNAToolView::IsAutoRigHelperJointsEnabled));

	const TSharedRef<SWidget> BodyMeshSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("ImportFromDNABodyMeshSectionLabel", "Body Mesh"))
		.Visibility(this, &SMetaHumanCharacterEditorImportFromDNAToolView::GetImportToolModeVisibility, EMetaHumanImportToolMode::Replace)
		.Content()
		[
			SNew(SVerticalBox)

			// Replace Mesh
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				ReplaceMeshWidget
			]

			// Auto-Rig Helper Joints
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				AutoRigHelperJointsWidget
			]
		];

	return BodyMeshSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorImportFromDNAToolView::CreateImportBodyJointsSection()
{
	UMetaHumanCharacterImportFromDNAProperties* ImportFromDNAProperties = Cast<UMetaHumanCharacterImportFromDNAProperties>(GetToolProperties());
	FConformBodyParams* BodyParams = IsValid(ImportFromDNAProperties) ? &ImportFromDNAProperties->BodyParams : nullptr;
	if (!BodyParams)
	{
		return SNullWidget::NullWidget;
	}

	UStruct* Struct = FConformBodyParams::StaticStruct();

	FProperty* ReplaceBodyJointsProperty = UMetaHumanCharacterImportFromDNAProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromDNAProperties, bReplaceBodyJoints));
	FProperty* ImportHelperJointsProperty = Struct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FConformBodyParams, bImportHelperJoints));

	const void* ImportFromDNAPropertiesDefault = ImportFromDNAProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterImportFromDNAProperties>();
	const void* BodyParamsDefault = &ImportFromDNAProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterImportFromDNAProperties>()->BodyParams;

	TSharedRef<SWidget> ReplaceBodyJointsWidget = CreatePropertyCheckBoxWidget(ReplaceBodyJointsProperty->GetDisplayNameText(), ReplaceBodyJointsProperty, ImportFromDNAProperties, ImportFromDNAPropertiesDefault);
	ReplaceBodyJointsWidget->SetEnabled(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateUObject(ImportFromDNAProperties, &UMetaHumanCharacterImportFromDNAProperties::CanImportJoints)));

	TSharedRef<SWidget> ImportHelperJointsWidget = CreatePropertyCheckBoxWidget(ImportHelperJointsProperty->GetDisplayNameText(), ImportHelperJointsProperty, BodyParams, BodyParamsDefault);
	ImportHelperJointsWidget->SetEnabled(TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorImportFromDNAToolView::IsImportHelperJointsEnabled));

	const TSharedRef<SWidget> BodyJointsSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("ImportFromDNABodyJointsSectionLabel", "Body Joints"))
		.Visibility(this, &SMetaHumanCharacterEditorImportFromDNAToolView::GetImportToolModeVisibility, EMetaHumanImportToolMode::Replace)
		.Content()
		[
			SNew(SVerticalBox)

			// Replace Body Joints
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				ReplaceBodyJointsWidget
			]

			// Import Helper Joints
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				ImportHelperJointsWidget
			]
		];

	return BodyJointsSectionWidget;
}

bool SMetaHumanCharacterEditorImportFromDNAToolView::IsDNABodyEnabled() const
{
	const UMetaHumanCharacterImportFromDNAProperties* ImportFromDNAProperties = Cast<UMetaHumanCharacterImportFromDNAProperties>(GetToolProperties());
	return ImportFromDNAProperties && !ImportFromDNAProperties->ImportOptions.bIsolateHeadFromBody;
}

bool SMetaHumanCharacterEditorImportFromDNAToolView::IsReplaceMeshEnabled() const
{
	const UMetaHumanCharacterImportFromDNAProperties* ImportFromDNAProperties = Cast<UMetaHumanCharacterImportFromDNAProperties>(GetToolProperties());
	return
		ImportFromDNAProperties &&
		!ImportFromDNAProperties->ImportOptions.bImportWholeRig &&
		ImportFromDNAProperties->CanImportMesh();
}

bool SMetaHumanCharacterEditorImportFromDNAToolView::IsAutoRigHelperJointsEnabled() const
{
	const UMetaHumanCharacterImportFromDNAProperties* ImportFromDNAProperties = Cast<UMetaHumanCharacterImportFromDNAProperties>(GetToolProperties());
	return 
		ImportFromDNAProperties &&
		!ImportFromDNAProperties->ImportOptions.bImportWholeRig &&
		ImportFromDNAProperties->bReplaceMesh &&
		ImportFromDNAProperties->CanImportMesh();
}

bool SMetaHumanCharacterEditorImportFromDNAToolView::IsImportHelperJointsEnabled() const
{
	const UMetaHumanCharacterImportFromDNAProperties* ImportFromDNAProperties = Cast<UMetaHumanCharacterImportFromDNAProperties>(GetToolProperties());
	return
		ImportFromDNAProperties &&
		ImportFromDNAProperties->bReplaceBodyJoints &&
		ImportFromDNAProperties->CanImportJoints();
}

void SMetaHumanCharacterEditorImportFromIdentityToolView::Construct(const FArguments& InArgs, UMetaHumanCharacterEditorImportTool* InTool)
{
	SMetaHumanCharacterEditorInternalImportToolView::Construct(SMetaHumanCharacterEditorInternalImportToolView::FArguments(), InTool);
}

void SMetaHumanCharacterEditorImportFromIdentityToolView::MakeToolView()
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
					CreateImportToolViewWarningSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateImportLoadIdentityAssetsSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateImportViewOptionsSection()
				]
			];
	}

	if (ToolViewMainBox.IsValid())
	{
		ToolViewMainBox->AddSlot()
			.Padding(0.f, 4.f, 0.f, 0.f)
			.AutoHeight()
			[
				CreateImportToolViewButtonSection()
			];
	}
}

TSharedRef<SWidget> SMetaHumanCharacterEditorImportFromIdentityToolView::CreateImportLoadIdentityAssetsSection()
{
	UMetaHumanCharacterImportFromIdentityProperties* ImportFromIdentityProperties = Cast<UMetaHumanCharacterImportFromIdentityProperties>(GetToolProperties());
	if (!ImportFromIdentityProperties)
	{
		return SNullWidget::NullWidget;
	}

	FProperty* MetaHumanIdentityProperty = UMetaHumanCharacterImportFromIdentityProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromIdentityProperties, MetaHumanIdentity));
	
	const TSharedRef<SWidget> LoadIdentityAssetsSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("LoadIdentityAssetsSectionLabel", "Load Identity Assets"))
		.Content()
		[
			SNew(SVerticalBox)
		
			// MetaHuman Identity
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyDefaultWidget(MetaHumanIdentityProperty->GetDisplayNameText(), MetaHumanIdentityProperty, ImportFromIdentityProperties, this)
			]
		];

	return LoadIdentityAssetsSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorImportFromIdentityToolView::CreateImportViewOptionsSection()
{
	UMetaHumanCharacterImportFromIdentityProperties* ImportFromIdentityProperties = Cast<UMetaHumanCharacterImportFromIdentityProperties>(GetToolProperties());
	FImportFromIdentityParams* ImportFromIdentityParams = IsValid(ImportFromIdentityProperties) ? &ImportFromIdentityProperties->ImportOptions : nullptr;
	if (!ImportFromIdentityParams)
	{
		return SNullWidget::NullWidget;
	}

	UStruct* Struct = FImportFromIdentityParams::StaticStruct();

	FProperty* UseEyeMeshesProperty = Struct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FImportFromIdentityParams, bUseEyeMeshes));
	FProperty* UseTeethMeshProperty = Struct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FImportFromIdentityParams, bUseTeethMesh));
	FProperty* UseMetricScaleProperty = Struct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FImportFromIdentityParams, bUseMetricScale));

	const void* ImportParamsDefault = &ImportFromIdentityProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterImportFromIdentityProperties>()->ImportOptions;

	const TSharedRef<SWidget> ViewOptionsSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("ImportFromIdentityViewOptionsSectionLabel", "View Options"))
		.Content()
		[
			SNew(SVerticalBox)

			// Use Eye Meshes
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyCheckBoxWidget(UseEyeMeshesProperty->GetDisplayNameText(), UseEyeMeshesProperty, ImportFromIdentityParams, ImportParamsDefault)
			]

			// Use Teeth Mesh
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyCheckBoxWidget(UseTeethMeshProperty->GetDisplayNameText(), UseTeethMeshProperty, ImportFromIdentityParams, ImportParamsDefault)
			]

			// Use Metric Scale
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyCheckBoxWidget(UseMetricScaleProperty->GetDisplayNameText(), UseMetricScaleProperty, ImportFromIdentityParams, ImportParamsDefault)
			]
		];

	return ViewOptionsSectionWidget;
}

FText SMetaHumanCharacterEditorImportFromIdentityToolView::GetWarningText() const
{
	return LOCTEXT("IdentityImportMeshFitWarningText", "Use a MetaHuman Identity asset to generate a fully rigged, animation-ready MetaHuman.");
}

void SMetaHumanCharacterEditorImportFromTemplateToolView::Construct(const FArguments& InArgs, UMetaHumanCharacterEditorImportTool* InTool)
{
	SMetaHumanCharacterEditorInternalImportToolView::Construct(SMetaHumanCharacterEditorInternalImportToolView::FArguments(), InTool);
}

void SMetaHumanCharacterEditorImportFromTemplateToolView::MakeToolView()
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
					CreateImportToolToolbarSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateImportToolViewWarningSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateImportLoadTemplateAssetsSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateImportHeadOptionsSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateImportBodyOptionsSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateImportBodyMeshSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateImportBodyJointsSection()
				]
			];
	}

	if (ToolViewMainBox.IsValid())
	{
		ToolViewMainBox->AddSlot()
			.Padding(0.f, 4.f, 0.f, 0.f)
			.AutoHeight()
			[
				CreateImportToolViewButtonSection()
			];
	}
}

bool SMetaHumanCharacterEditorImportFromTemplateToolView::IsImportButtonEnabled() const
{
	const UMetaHumanCharacterImportFromTemplateProperties* ImportFromTemplateProperties = Cast<UMetaHumanCharacterImportFromTemplateProperties>(GetToolProperties());
	if (ImportFromTemplateProperties)
	{
		switch (ImportFromTemplateProperties->Mode)
		{
		case EMetaHumanImportToolMode::MeshFit:
			return ImportFromTemplateProperties->CanConform();
		case EMetaHumanImportToolMode::Replace:
			return ImportFromTemplateProperties->CanImportMesh();
		}
	}

	return false;
}

FReply SMetaHumanCharacterEditorImportFromTemplateToolView::OnImportButtonClicked()
{
	UMetaHumanCharacterImportFromTemplateProperties* ImportFromTemplateProperties = Cast<UMetaHumanCharacterImportFromTemplateProperties>(GetToolProperties());
	if (ImportFromTemplateProperties)
	{
		switch (ImportFromTemplateProperties->Mode)
		{
		case EMetaHumanImportToolMode::MeshFit:
			ImportFromTemplateProperties->Conform();
			break;
		case EMetaHumanImportToolMode::Replace:
			if (ImportFromTemplateProperties->bReplaceMesh)
			{
				ImportFromTemplateProperties->ImportMesh();
			}
			if (ImportFromTemplateProperties->bReplaceBodyJoints && ImportFromTemplateProperties->CanImportJoints())
			{
				ImportFromTemplateProperties->ImportJoints();
			}
			break;
		}
	}

	return FReply::Handled();
}

FText SMetaHumanCharacterEditorImportFromTemplateToolView::GetWarningText() const
{
	const UMetaHumanCharacterImportSubToolBase* ImportToolProperties = Cast<UMetaHumanCharacterImportSubToolBase>(GetToolProperties());
	if (ImportToolProperties)
	{
		switch (ImportToolProperties->Mode)
		{
		case EMetaHumanImportToolMode::MeshFit:
			return LOCTEXT("TemplateImportMeshFitWarningText", "Use a modified MetaHuman template mesh to reshape the existing MetaHuman so it matches your custom template.");
		case EMetaHumanImportToolMode::Replace:
			return LOCTEXT("TemplateImportReplaceWarningText", "Use a custom mesh and/or joints to replace the current MetaHuman body while keeping the existing structure intact. This workflow maps your mesh onto the MetaHuman framework without rebuilding the character from scratch.");
		}
	}

	return FText::GetEmpty();
}

TSharedRef<SWidget> SMetaHumanCharacterEditorImportFromTemplateToolView::CreateImportLoadTemplateAssetsSection()
{
	UMetaHumanCharacterImportFromTemplateProperties* ImportFromTemplateProperties = Cast<UMetaHumanCharacterImportFromTemplateProperties>(GetToolProperties());
	if (!ImportFromTemplateProperties)
	{
		return SNullWidget::NullWidget;
	}

	FProperty* HeadMeshProperty = UMetaHumanCharacterImportFromTemplateProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromTemplateProperties, HeadMesh));
	FProperty* LeftEyeMeshProperty = UMetaHumanCharacterImportFromTemplateProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromTemplateProperties, LeftEyeMesh));
	FProperty* RightEyeMeshProperty = UMetaHumanCharacterImportFromTemplateProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromTemplateProperties, RightEyeMesh));
	FProperty* TeethMeshProperty = UMetaHumanCharacterImportFromTemplateProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromTemplateProperties, TeethMesh));
	FProperty* BodyMeshProperty = UMetaHumanCharacterImportFromTemplateProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromTemplateProperties, BodyMesh));
	FProperty* MatchVerticesProperty = UMetaHumanCharacterImportFromTemplateProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromTemplateProperties, bMatchVerticesByUVs));

	const void* ImportFromTemplatePropertiesDefault = ImportFromTemplateProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterImportFromTemplateProperties>();

	TSharedRef<SWidget> HeadMeshWidget = CreatePropertyDefaultWidget(HeadMeshProperty->GetDisplayNameText(), HeadMeshProperty, ImportFromTemplateProperties, this);
	HeadMeshWidget->SetVisibility(TAttribute<EVisibility>::CreateSP(this, &SMetaHumanCharacterEditorImportFromTemplateToolView::GetImportToolModeVisibility, EMetaHumanImportToolMode::MeshFit));

	TSharedRef<SWidget> HeadMeshForNeckRegionWidget = CreatePropertyDefaultWidget(LOCTEXT("LoadTemplateAssets_HeadMeshForNeckRegionLabel", "Head Mesh for Neck Region"), HeadMeshProperty, ImportFromTemplateProperties, this);
	HeadMeshForNeckRegionWidget->SetVisibility(TAttribute<EVisibility>::CreateSP(this, &SMetaHumanCharacterEditorImportFromTemplateToolView::GetImportToolModeVisibility, EMetaHumanImportToolMode::Replace));

	TSharedRef<SWidget> LeftEyeMeshWidget = CreatePropertyDefaultWidget(LeftEyeMeshProperty->GetDisplayNameText(), LeftEyeMeshProperty, ImportFromTemplateProperties, this);
	LeftEyeMeshWidget->SetEnabled(TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorImportFromTemplateToolView::IsHeadMeshPropertyEnabledByClass, UStaticMesh::StaticClass()));

	TSharedRef<SWidget> RightEyeMeshWidget = CreatePropertyDefaultWidget(RightEyeMeshProperty->GetDisplayNameText(), RightEyeMeshProperty, ImportFromTemplateProperties, this);
	RightEyeMeshWidget->SetEnabled(TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorImportFromTemplateToolView::IsHeadMeshPropertyEnabledByClass, UStaticMesh::StaticClass()));
	
	TSharedRef<SWidget> TeethMeshWidget = CreatePropertyDefaultWidget(TeethMeshProperty->GetDisplayNameText(), TeethMeshProperty, ImportFromTemplateProperties, this);
	TeethMeshWidget->SetEnabled(TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorImportFromTemplateToolView::IsHeadMeshPropertyEnabledByClass, UStaticMesh::StaticClass()));

	TSharedRef<SWidget> BodyMeshWidget = CreatePropertyDefaultWidget(BodyMeshProperty->GetDisplayNameText(), BodyMeshProperty, ImportFromTemplateProperties, this);
	BodyMeshWidget->SetEnabled(TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorImportFromTemplateToolView::IsBodyMeshEnabled));

	const TSharedRef<SWidget> LoadTemplateAssetsSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("LoadTemplateAssetsSectionLabel", "Assets"))
		.Content()
		[
			SNew(SVerticalBox)

			// Head Mesh
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				HeadMeshWidget
			]

			// Left Eye Mesh
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SMetaHumanCharacterEditorToolPanel)
				.Label(LOCTEXT("LoadTemplateAssetsSection_MeshPartsLabel", "Head Mesh Parts"))
				.RoundedBorders(false)
				.HierarchyLevel(EMetaHumanCharacterEditorPanelHierarchyLevel::Middle)
				.Visibility(this, &SMetaHumanCharacterEditorImportFromTemplateToolView::GetImportToolModeVisibility, EMetaHumanImportToolMode::MeshFit)
				.Content()
				[
					SNew(SVerticalBox)

					// Left Eye Mesh
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						LeftEyeMeshWidget
					]
					// Right Eye Mesh
					+SVerticalBox::Slot()
					.AutoHeight()
					[
						RightEyeMeshWidget
					]

					// Teeth Mesh
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						TeethMeshWidget
					]
				]
			]

			// Body Mesh
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BodyMeshWidget
			]

			// Head Mesh
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				HeadMeshForNeckRegionWidget
			]

			// Match Vertices by UVs
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyCheckBoxWidget(MatchVerticesProperty->GetDisplayNameText(), MatchVerticesProperty, ImportFromTemplateProperties, ImportFromTemplatePropertiesDefault)
			]
		];

	return LoadTemplateAssetsSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorImportFromTemplateToolView::CreateImportHeadOptionsSection()
{
	UMetaHumanCharacterImportFromTemplateProperties* ImportFromTemplateProperties = Cast<UMetaHumanCharacterImportFromTemplateProperties>(GetToolProperties());
	if (!ImportFromTemplateProperties)
	{
		return SNullWidget::NullWidget;
	}

	FProperty* AlignScaleProperty = UMetaHumanCharacterImportFromTemplateProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromTemplateProperties, bAlignScale));
	FProperty* AlignRotationProperty = UMetaHumanCharacterImportFromTemplateProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromTemplateProperties, bAlignRotation));
	FProperty* AlignTranslationProperty = UMetaHumanCharacterImportFromTemplateProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromTemplateProperties, bAlignTranslation));

	const void* ImportFromTemplatePropertiesDefault = ImportFromTemplateProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterImportFromTemplateProperties>();

	const TSharedRef<SWidget> HeadOptionsSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("ImportFromTemplateHeadOptionsSectionLabel", "Head Options"))
		.Visibility(this, &SMetaHumanCharacterEditorImportFromTemplateToolView::GetImportToolModeVisibility, EMetaHumanImportToolMode::MeshFit)
		.Content()
		[
			SNew(SVerticalBox)

			// Align Scale
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyCheckBoxWidget(AlignScaleProperty->GetDisplayNameText(), AlignScaleProperty, ImportFromTemplateProperties, ImportFromTemplatePropertiesDefault)
			]

			// Align Rotation
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyCheckBoxWidget(AlignRotationProperty->GetDisplayNameText(), AlignRotationProperty, ImportFromTemplateProperties, ImportFromTemplatePropertiesDefault)
			]

			// Align Translation
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyCheckBoxWidget(AlignTranslationProperty->GetDisplayNameText(), AlignTranslationProperty, ImportFromTemplateProperties, ImportFromTemplatePropertiesDefault)
			]
		];

	return HeadOptionsSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorImportFromTemplateToolView::CreateImportBodyOptionsSection()
{
	UMetaHumanCharacterImportFromTemplateProperties* ImportFromTemplateProperties = Cast<UMetaHumanCharacterImportFromTemplateProperties>(GetToolProperties());
	FConformBodyParams* BodyParams = IsValid(ImportFromTemplateProperties) ? &ImportFromTemplateProperties->BodyParams : nullptr;
	FImportFromTemplateParams* ImportParams = IsValid(ImportFromTemplateProperties) ? &ImportFromTemplateProperties->ImportOptions : nullptr;
	if (!BodyParams || !ImportParams)
	{
		return SNullWidget::NullWidget;
	}

	UStruct* BodyStruct = FConformBodyParams::StaticStruct();
	UStruct* ImportStruct = FImportFromTemplateParams::StaticStruct();

	FProperty* TargetIsInMetaHumanAPoseProperty = BodyStruct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FConformBodyParams, bTargetIsInMetaHumanAPose));
	FProperty* EstimateJointsFromMeshProperty = BodyStruct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FConformBodyParams, bEstimateJointsFromMesh));
	FProperty* IsolateHeadFromBodyProperty = ImportStruct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FImportFromTemplateParams, bIsolateHeadFromBody));

	const void* BodyParamsDefault = &ImportFromTemplateProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterImportFromTemplateProperties>()->BodyParams;
	const void* ImportOptionsDefault = &ImportFromTemplateProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterImportFromTemplateProperties>()->ImportOptions;

	const TSharedRef<SWidget> BodyMeshSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("ImportFromTemplateBodyOptionsSectionLabel", "Body Options"))
		.Visibility(this, &SMetaHumanCharacterEditorImportFromTemplateToolView::GetImportToolModeVisibility, EMetaHumanImportToolMode::MeshFit)
		.Content()
		[
			SNew(SVerticalBox)

			// Target is in MetaHuman A-Pose
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyCheckBoxWidget(TargetIsInMetaHumanAPoseProperty->GetDisplayNameText(), TargetIsInMetaHumanAPoseProperty, BodyParams, BodyParamsDefault)
			]

			// Advanced section
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SMetaHumanCharacterEditorToolPanel)
				.Label(LOCTEXT("ImportFromTemplateBodyOptionsAdvancedSectionLabel", "Advanced"))
				.RoundedBorders(false)
				.HierarchyLevel(EMetaHumanCharacterEditorPanelHierarchyLevel::Middle)
				.Content()
				[
					SNew(SVerticalBox)

					// Estimate Joints from Mesh
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						CreatePropertyCheckBoxWidget(EstimateJointsFromMeshProperty->GetDisplayNameText(), EstimateJointsFromMeshProperty, BodyParams, BodyParamsDefault)
					]

					// Adopt Custom Neck
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						CreatePropertyCheckBoxWidget(IsolateHeadFromBodyProperty->GetDisplayNameText(), IsolateHeadFromBodyProperty, ImportParams, ImportOptionsDefault)
					]
				]
			]
		];

	return BodyMeshSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorImportFromTemplateToolView::CreateImportBodyMeshSection()
{
	UMetaHumanCharacterImportFromTemplateProperties* ImportFromTemplateProperties = Cast<UMetaHumanCharacterImportFromTemplateProperties>(GetToolProperties());
	FConformBodyParams* BodyParams = IsValid(ImportFromTemplateProperties) ? &ImportFromTemplateProperties->BodyParams : nullptr;
	if (!BodyParams)
	{
		return SNullWidget::NullWidget;
	}

	UStruct* Struct = FConformBodyParams::StaticStruct();

	FProperty* ReplaceMeshProperty = UMetaHumanCharacterImportFromTemplateProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromTemplateProperties, bReplaceMesh));
	FProperty* AutoRigHelperJointsProperty = Struct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FConformBodyParams, bAutoRigHelperJoints));

	const void* ImportFromTemplatePropertiesDefault = ImportFromTemplateProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterImportFromTemplateProperties>();
	const void* BodyParamsDefault = &ImportFromTemplateProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterImportFromTemplateProperties>()->BodyParams;

	TSharedRef<SWidget> ReplaceMeshWidget = CreatePropertyCheckBoxWidget(ReplaceMeshProperty->GetDisplayNameText(), ReplaceMeshProperty, ImportFromTemplateProperties, ImportFromTemplatePropertiesDefault);
	ReplaceMeshWidget->SetEnabled(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateUObject(ImportFromTemplateProperties, &UMetaHumanCharacterImportFromTemplateProperties::CanImportMesh)));

	TSharedRef<SWidget> AutoRigHelperJointsWidget = CreatePropertyCheckBoxWidget(AutoRigHelperJointsProperty->GetDisplayNameText(), AutoRigHelperJointsProperty, BodyParams, BodyParamsDefault);
	AutoRigHelperJointsWidget->SetEnabled(TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorImportFromTemplateToolView::IsAutoRigHelperJointsEnabled));

	const TSharedRef<SWidget> BodyMeshSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("ImportFromTemplateBodyMeshSectionLabel", "Body Mesh"))
		.Visibility(this, &SMetaHumanCharacterEditorImportFromTemplateToolView::GetImportToolModeVisibility, EMetaHumanImportToolMode::Replace)
		.Content()
		[
			SNew(SVerticalBox)

			// Replace Mesh
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				ReplaceMeshWidget
			]

			// Auto-Rig Helper Joints
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				AutoRigHelperJointsWidget
			]
		];

	return BodyMeshSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorImportFromTemplateToolView::CreateImportBodyJointsSection()
{
	UMetaHumanCharacterImportFromTemplateProperties* ImportFromTemplateProperties = Cast<UMetaHumanCharacterImportFromTemplateProperties>(GetToolProperties());
	FConformBodyParams* BodyParams = IsValid(ImportFromTemplateProperties) ? &ImportFromTemplateProperties->BodyParams : nullptr;
	if (!BodyParams)
	{
		return SNullWidget::NullWidget;
	}

	UStruct* Struct = FConformBodyParams::StaticStruct();

	FProperty* ReplaceBodyJointsProperty = UMetaHumanCharacterImportFromTemplateProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterImportFromTemplateProperties, bReplaceBodyJoints));
	FProperty* ImportHelperJointsProperty = Struct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FConformBodyParams, bImportHelperJoints));

	const void* ImportFromTemplatePropertiesDefault = ImportFromTemplateProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterImportFromTemplateProperties>();
	const void* BodyParamsDefault = &ImportFromTemplateProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterImportFromTemplateProperties>()->BodyParams;

	TSharedRef<SWidget> ReplaceBodyJointsWidget = CreatePropertyCheckBoxWidget(ReplaceBodyJointsProperty->GetDisplayNameText(), ReplaceBodyJointsProperty, ImportFromTemplateProperties, ImportFromTemplatePropertiesDefault);
	ReplaceBodyJointsWidget->SetEnabled(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateUObject(ImportFromTemplateProperties, &UMetaHumanCharacterImportFromTemplateProperties::CanImportJoints)));

	TSharedRef<SWidget> ImportHelperJointsWidget = CreatePropertyCheckBoxWidget(ImportHelperJointsProperty->GetDisplayNameText(), ImportHelperJointsProperty, BodyParams, BodyParamsDefault);
	ImportHelperJointsWidget->SetEnabled(TAttribute<bool>::CreateSP(this, &SMetaHumanCharacterEditorImportFromTemplateToolView::IsImportHelperJointsEnabled));

	const TSharedRef<SWidget> BodyJointsSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("ImportFromTemplateBodyJointsSectionLabel", "Body Joints"))
		.Visibility(this, &SMetaHumanCharacterEditorImportFromTemplateToolView::GetImportToolModeVisibility, EMetaHumanImportToolMode::Replace)
		.Content()
		[
			SNew(SVerticalBox)

				// Replace Body Joints
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					ReplaceBodyJointsWidget
				]

				// Import Helper Joints
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					ImportHelperJointsWidget
				]
		];

	return BodyJointsSectionWidget;
}

bool SMetaHumanCharacterEditorImportFromTemplateToolView::IsBodyMeshEnabled() const
{
	const UMetaHumanCharacterImportFromTemplateProperties* ImportFromTemplateProperties = Cast<UMetaHumanCharacterImportFromTemplateProperties>(GetToolProperties());
	return ImportFromTemplateProperties && !ImportFromTemplateProperties->ImportOptions.bIsolateHeadFromBody;
}

bool SMetaHumanCharacterEditorImportFromTemplateToolView::IsHeadMeshPropertyEnabledByClass(TSubclassOf<UStreamableRenderAsset> Class) const
{
	bool bIsEnabled = false;
	
	const UMetaHumanCharacterImportFromTemplateProperties* ImportFromTemplateProperties = Cast<UMetaHumanCharacterImportFromTemplateProperties>(GetToolProperties());
	if (ImportFromTemplateProperties && Class)
	{
		const FSoftObjectPath AssetPath = ImportFromTemplateProperties->HeadMesh.ToSoftObjectPath();
		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(AssetPath);
		bIsEnabled = AssetData.IsValid() && AssetData.AssetClassPath == Class->GetClassPathName();
	}
				
	return bIsEnabled;
}

bool SMetaHumanCharacterEditorImportFromTemplateToolView::IsAutoRigHelperJointsEnabled() const
{
	const UMetaHumanCharacterImportFromTemplateProperties* ImportFromTemplateProperties = Cast<UMetaHumanCharacterImportFromTemplateProperties>(GetToolProperties());
	return 
		ImportFromTemplateProperties && 
		ImportFromTemplateProperties->bReplaceMesh &&
		ImportFromTemplateProperties->CanImportMesh();
}

bool SMetaHumanCharacterEditorImportFromTemplateToolView::IsImportHelperJointsEnabled() const
{
	const UMetaHumanCharacterImportFromTemplateProperties* ImportFromTemplateProperties = Cast<UMetaHumanCharacterImportFromTemplateProperties>(GetToolProperties());
	return 
		ImportFromTemplateProperties && 
		ImportFromTemplateProperties->bReplaceBodyJoints &&
		ImportFromTemplateProperties->CanImportJoints();
}

#undef LOCTEXT_NAMESPACE
