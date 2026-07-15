// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditorBlendToolView.h"

#include "Engine/Texture2D.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "Slate/DeferredCleanupSlateBrush.h"
#include "Tools/MetaHumanCharacterEditorBodyEditingTools.h"
#include "Tools/MetaHumanCharacterEditorFaceEditingTools.h"
#include "Tools/MetaHumanCharacterEditorToolTargetUtil.h"
#include "UI/Widgets/SMetaHumanCharacterEditorBlendToolPanel.h"
#include "UI/Widgets/SMetaHumanCharacterEditorToolPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "SWarningOrErrorBox.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCharacterEditorBlendToolView"

namespace UE::MetaHuman::Private
{
	static const FName BlendToolViewNameID = FName(TEXT("BlendToolView"));
}

FName SMetaHumanCharacterEditorBlendToolView::AssetsSlotName(TEXT("Presets Library"));

SLATE_IMPLEMENT_WIDGET(SMetaHumanCharacterEditorBlendToolView)
void SMetaHumanCharacterEditorBlendToolView::PrivateRegisterAttributes(FSlateAttributeInitializer& InAttributeInitializer)
{
}

void SMetaHumanCharacterEditorBlendToolView::Construct(const FArguments& InArgs, UMetaHumanCharacterEditorMeshBlendTool* InTool)
{
	SMetaHumanCharacterEditorToolView::Construct(SMetaHumanCharacterEditorToolView::FArguments(), InTool);
}

const FName& SMetaHumanCharacterEditorBlendToolView::GetToolViewNameID() const
{
	return UE::MetaHuman::Private::BlendToolViewNameID;
}

UInteractiveToolPropertySet* SMetaHumanCharacterEditorBlendToolView::GetToolProperties() const
{
	UMetaHumanCharacterEditorMeshBlendTool* BlendTool = Cast<UMetaHumanCharacterEditorMeshBlendTool>(Tool);
	if (!IsValid(BlendTool))
	{
		return nullptr;
	}
	return BlendTool->GetBlendToolProperties();
}

void SMetaHumanCharacterEditorBlendToolView::MakeToolView()
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
					CreateBlendToolViewBlendPanelSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateParametersViewSection()
				]

				+ SVerticalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Bottom)
				.Padding(4.f)
				.AutoHeight()
				[
					CreateHeadParametersViewSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateBodyParametersViewSection()
				]
			];
	}

	if (BlendToolPanel.IsValid())
	{
		const UMetaHumanCharacterEditorMeshBlendTool* BlendTool = Cast<UMetaHumanCharacterEditorMeshBlendTool>(Tool);
		if (const UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties* BlendToolProperties = Cast<UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties>(BlendTool->GetBlendToolProperties()))
		{
			BlendToolPanel->RestorePresets(BlendToolProperties->PresetCharacters);
		}
	}
}

FMetaHumanCharacterAssetViewsPanelStatus SMetaHumanCharacterEditorBlendToolView::GetAssetViewsPanelStatus() const
{
	FMetaHumanCharacterAssetViewsPanelStatus Status;

	const TSharedPtr<SMetaHumanCharacterEditorAssetViewsPanel> AssetViewsPanel = BlendToolPanel.IsValid() ? BlendToolPanel->GetAssetViewsPanel() : nullptr;
	if (AssetViewsPanel.IsValid())
	{
		Status = AssetViewsPanel->GetAssetViewsPanelStatus();
	}

	Status.ToolViewName = GetToolViewNameID();
	return Status;
}

void SMetaHumanCharacterEditorBlendToolView::SetAssetViewsPanelStatus(const FMetaHumanCharacterAssetViewsPanelStatus& Status)
{
	const TSharedPtr<SMetaHumanCharacterEditorAssetViewsPanel> AssetViewsPanel = BlendToolPanel.IsValid() ? BlendToolPanel->GetAssetViewsPanel() : nullptr;
	if (AssetViewsPanel.IsValid() && Status.ToolViewName == GetToolViewNameID())
	{
		AssetViewsPanel->UpdateAssetViewsPanelStatus(Status);
	}
}

TArray<FMetaHumanCharacterAssetViewStatus> SMetaHumanCharacterEditorBlendToolView::GetAssetViewsStatusArray() const
{
	TArray<FMetaHumanCharacterAssetViewStatus> StatusArray;
	
	const TSharedPtr<SMetaHumanCharacterEditorAssetViewsPanel> AssetViewsPanel = BlendToolPanel.IsValid() ? BlendToolPanel->GetAssetViewsPanel() : nullptr;
	if (AssetViewsPanel.IsValid())
	{
		StatusArray = AssetViewsPanel->GetAssetViewsStatusArray();
	}

	return StatusArray;
}

void SMetaHumanCharacterEditorBlendToolView::SetAssetViewsStatus(const TArray<FMetaHumanCharacterAssetViewStatus>& StatusArray)
{
	const TSharedPtr<SMetaHumanCharacterEditorAssetViewsPanel> AssetViewsPanel = BlendToolPanel.IsValid() ? BlendToolPanel->GetAssetViewsPanel() : nullptr;
	if (AssetViewsPanel.IsValid() && !StatusArray.IsEmpty())
	{
		AssetViewsPanel->UpdateAssetViewsStatus(StatusArray);
	}
}

void SMetaHumanCharacterEditorBlendToolView::OnBlendToolItemContentSet(const FAssetData& InAssetData, int32 InItemIndex)
{
	UMetaHumanCharacterEditorMeshBlendTool* BlendTool = Cast<UMetaHumanCharacterEditorMeshBlendTool>(Tool);
	if (UMetaHumanCharacter* Character = Cast<UMetaHumanCharacter>(InAssetData.GetAsset()))
	{
		BlendTool->AddMetaHumanCharacterPreset(Character, InItemIndex);

		if (UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties* BlendToolProperties = Cast<UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties>(BlendTool->GetBlendToolProperties()))
		{
			if (BlendToolProperties->PresetCharacters.Num() <= InItemIndex)
			{
				BlendToolProperties->PresetCharacters.AddDefaulted(InItemIndex - BlendToolProperties->PresetCharacters.Num() + 1);
			}
			BlendToolProperties->PresetCharacters[InItemIndex] = Character;
		}
	}
}

void SMetaHumanCharacterEditorBlendToolView::OnBlendToolItemContentDeleted(int32 InItemIndex)
{
	UMetaHumanCharacterEditorMeshBlendTool* BlendTool = Cast<UMetaHumanCharacterEditorMeshBlendTool>(Tool);
	if (BlendTool)
	{
		BlendTool->RemoveMetaHumanCharacterPreset(InItemIndex);

		if (UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties* BlendToolProperties = Cast<UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties>(BlendTool->GetBlendToolProperties()))
		{
			if (InItemIndex < BlendToolProperties->PresetCharacters.Num())
			{
				BlendToolProperties->PresetCharacters[InItemIndex].Reset();
			}
		}
	}
}

void SMetaHumanCharacterEditorBlendToolView::OnBlendToolItemActivated(TSharedPtr<FMetaHumanCharacterAssetViewItem> Item)
{
	UMetaHumanCharacterEditorMeshBlendTool* BlendTool = Cast<UMetaHumanCharacterEditorMeshBlendTool>(Tool);
	if (BlendTool && Item.IsValid())
	{
		const FAssetData AssetData = Item->AssetData;
		if (UMetaHumanCharacter* Character = Cast<UMetaHumanCharacter>(AssetData.GetAsset()))
		{
			BlendTool->BlendToMetaHumanCharacterPreset(Character);
		}
	}
}

void SMetaHumanCharacterEditorBlendToolView::OnBlendToolItemApplyHeadOnly(TSharedPtr<FMetaHumanCharacterAssetViewItem> Item)
{
	UMetaHumanCharacterEditorMeshBlendTool* BlendTool = Cast<UMetaHumanCharacterEditorMeshBlendTool>(Tool);
	if (BlendTool && Item.IsValid())
	{
		const FAssetData AssetData = Item->AssetData;
		if (UMetaHumanCharacter* Character = Cast<UMetaHumanCharacter>(AssetData.GetAsset()))
		{
			BlendTool->BlendToMetaHumanCharacterPresetHeadOnly(Character);
		}
	}
}

void SMetaHumanCharacterEditorBlendToolView::OnBlendToolItemApplyBodyOnly(TSharedPtr<FMetaHumanCharacterAssetViewItem> Item)
{
	UMetaHumanCharacterEditorMeshBlendTool* BlendTool = Cast<UMetaHumanCharacterEditorMeshBlendTool>(Tool);
	if (BlendTool && Item.IsValid())
	{
		const FAssetData AssetData = Item->AssetData;
		if (UMetaHumanCharacter* Character = Cast<UMetaHumanCharacter>(AssetData.GetAsset()))
		{
			BlendTool->BlendToMetaHumanCharacterPresetBodyOnly(Character);
		}
	}
}

TSharedRef<SWidget> SMetaHumanCharacterEditorBlendToolView::CreateBlendToolViewBlendPanelSection()
{
	UMetaHumanCharacterEditorMeshBlendTool* BlendTool = Cast<UMetaHumanCharacterEditorMeshBlendTool>(Tool);
	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(BlendTool->GetTarget());

	return
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SMetaHumanCharacterEditorToolPanel)
			.Label(LOCTEXT("BodyBlendSectionLabel", "Blend Preset Selection"))
			.Visibility(this, &SMetaHumanCharacterEditorBlendToolView::GetBlendSubToolVisibility)
			.Content()
			[
				SNew(SVerticalBox)

				// Presets tile view section
				+ SVerticalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.Padding(4.f)
				.AutoHeight()
				[
					SAssignNew(BlendToolPanel, SMetaHumanCharacterEditorBlendToolPanel, Character, BlendTool)
					.VirtualFolderSlotName(AssetsSlotName)
					.OnItemContentSet(this, &SMetaHumanCharacterEditorBlendToolView::OnBlendToolItemContentSet)
					.OnItemDeleted(this, &SMetaHumanCharacterEditorBlendToolView::OnBlendToolItemContentDeleted)
					.OnItemActivated(this, &SMetaHumanCharacterEditorBlendToolView::OnBlendToolItemActivated)
					.OnItemApplyHeadOnly(this, &SMetaHumanCharacterEditorBlendToolView::OnBlendToolItemApplyHeadOnly)
					.OnItemApplyBodyOnly(this, &SMetaHumanCharacterEditorBlendToolView::OnBlendToolItemApplyBodyOnly)
					.OnOverrideItemThumbnail(this, &SMetaHumanCharacterEditorBlendToolView::OnOverrideItemThumbnailBrush)
					.OnFilterAssetData(this, &SMetaHumanCharacterEditorBlendToolView::OnFilterAddAssetDataToAssetView)
				]
			]
		]

		+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SWarningOrErrorBox)
				.AutoWrapText(false)
				.Visibility(this, &SMetaHumanCharacterEditorBlendToolView::GetFixedBodyWarningVisibility)
				.MessageStyle(EMessageStyle::Warning)
				.Message(LOCTEXT("MetaHumanBodyBlendFixedWarning", "This Asset uses a Fixed Body type, Fixed Body types can't\n"
					"be modified or used for blending without fitting them first to\n"
					"the Parametric Model. This is an approximation and can\n"
					"result in some visual differences."))
			]

		+ SVerticalBox::Slot()
		.Padding(4.f)
		.AutoHeight()
		[
			SNew(SMetaHumanCharacterEditorToolPanel)
			.Label(LOCTEXT("BlendToolFixedBodyTypeLabel", "Fixed Body Type"))
			.Visibility(this, &SMetaHumanCharacterEditorBlendToolView::GetFixedBodyWarningVisibility)
			.Content()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.Padding(4.f)
				[
					SNew(SButton)
					.OnClicked(this, &SMetaHumanCharacterEditorBlendToolView::OnPerformParametricFitButtonClicked)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PerformParametricFit", "Perform Parametric Fit"))
					]
				]
			]
		];
}

TSharedRef<SWidget> SMetaHumanCharacterEditorBlendToolView::CreateParametersViewSection()
{
	const UMetaHumanCharacterEditorMeshEditingTool* MeshTool = Cast<UMetaHumanCharacterEditorMeshEditingTool>(Tool);
	if (!IsValid(MeshTool))
	{
		return SNullWidget::NullWidget;
	}

	UMetaHumanCharacterEditorMeshEditingToolProperties* ManipulatorProperties = Cast<UMetaHumanCharacterEditorMeshEditingToolProperties>(MeshTool->GetMeshEditingToolProperties());
	if (!IsValid(ManipulatorProperties))
	{
		return SNullWidget::NullWidget;
	}

	UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties* BlendToolProperties = Cast<UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties>(GetToolProperties());
	if (!IsValid(BlendToolProperties))
	{
		return SNullWidget::NullWidget;
	}

	FProperty* SizeProperty = UMetaHumanCharacterEditorMeshEditingToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshEditingToolProperties, Size));
	FProperty* ShowManipulatorsProperty = UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties, bShowManipulators));

	const void* BlendToolPropertiesDefault = BlendToolProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties>();
	const void* ManipulatorPropertiesDefault = ManipulatorProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorMeshEditingToolProperties>();

	return
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("BodyBlendToolParametersSection", "Parameters"))
		.Visibility(this, &SMetaHumanCharacterEditorBlendToolView::GetBlendSubToolVisibility)
		.Content()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertySpinBoxWidget(LOCTEXT("ManipulatorSize", "Manipulator Size"), SizeProperty, ManipulatorProperties, ManipulatorPropertiesDefault)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertyCheckBoxWidget(ShowManipulatorsProperty->GetDisplayNameText(), ShowManipulatorsProperty, BlendToolProperties, BlendToolPropertiesDefault)
			]
		];
}

TSharedRef<SWidget> SMetaHumanCharacterEditorBlendToolView::CreateHeadParametersViewSection()
{
	const UMetaHumanCharacterEditorMeshEditingTool* MeshTool = Cast<UMetaHumanCharacterEditorMeshEditingTool>(Tool);
	const UMetaHumanCharacterEditorFaceTool* FaceTool = Cast<UMetaHumanCharacterEditorFaceTool>(Tool);
	if (!IsValid(MeshTool) || !IsValid(FaceTool))
	{
		return SNullWidget::NullWidget;
	}

	UMetaHumanCharacterEditorMeshEditingToolProperties* ManipulatorProperties = Cast<UMetaHumanCharacterEditorMeshEditingToolProperties>(MeshTool->GetMeshEditingToolProperties());
	UMetaHumanCharacterEditorHeadAndBodyParameterProperties* HeadParameterProperties = Cast<UMetaHumanCharacterEditorHeadAndBodyParameterProperties>(FaceTool->GetHeadAndBodyParameterProperties());
	UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties* BlendToolProperties = Cast<UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties>(GetToolProperties());
	if (!IsValid(ManipulatorProperties) || !HeadParameterProperties || !IsValid(BlendToolProperties))
	{
		return SNullWidget::NullWidget;
	}

	FProperty* SymmetricProperty = UMetaHumanCharacterEditorMeshEditingToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshEditingToolProperties, bSymmetricModeling));
	FProperty* FaceBlendOptionsProperty = UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties, FaceBlendOptions));
	FProperty* DeltaProperty = UMetaHumanCharacterEditorHeadAndBodyParameterProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorHeadAndBodyParameterProperties, FaceDelta));
	FProperty* HeadScaleProperty = UMetaHumanCharacterEditorHeadAndBodyParameterProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorHeadAndBodyParameterProperties, HeadScale));

	const void* BlendToolPropertiesDefault = BlendToolProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties>();
	const void* ManipulatorPropertiesDefault = ManipulatorProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorMeshEditingToolProperties>();
	const void* HeadParameterPropertiesDefault = HeadParameterProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorHeadAndBodyParameterProperties>();

	TSharedRef<SVerticalBox> ContentBox = SNew(SVerticalBox);

	ContentBox->AddSlot()
		.MinHeight(24.f)
		.Padding(2.f, 0.f)
		.AutoHeight()
		[
			CreatePropertyComboBoxWidget<EBlendOptions>(FaceBlendOptionsProperty->GetDisplayNameText(), BlendToolProperties->FaceBlendOptions, FaceBlendOptionsProperty, BlendToolProperties, BlendToolPropertiesDefault)
		];

	ContentBox->AddSlot()
		.AutoHeight()
		[
			CreatePropertyCheckBoxWidget(SymmetricProperty->GetDisplayNameText(), SymmetricProperty, ManipulatorProperties, ManipulatorPropertiesDefault)
		];

	ContentBox->AddSlot()
		.AutoHeight()
		[
			CreatePropertySpinBoxWidget(LOCTEXT("FaceDelta", "Delta"), DeltaProperty, HeadParameterProperties, HeadParameterPropertiesDefault)
		];

	ContentBox->AddSlot()
		.AutoHeight()
		[
			CreatePropertySpinBoxWidget(HeadScaleProperty->GetDisplayNameText(), HeadScaleProperty, HeadParameterProperties, HeadParameterPropertiesDefault)
		];

	ContentBox->AddSlot()
		.Padding(4.f, 6.f, 4.f, 4.f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "Button")
			.ForegroundColor(FLinearColor::White)
			.OnClicked(this, &SMetaHumanCharacterEditorBlendToolView::OnResetNeckButtonClicked)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ToolTipText(LOCTEXT("BodyViewResetFaceNeckToolTip", "Reverts the neck region and aligns it to the body."))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BodyViewResetFaceNeck", "Align Neck to Body"))
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			]
		];

	return
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("BodyBlendToolHeadParametersSection", "Head Blend Settings"))
		.Content()
		[
			ContentBox
		];
}

TSharedRef<SWidget> SMetaHumanCharacterEditorBlendToolView::CreateBodyParametersViewSection()
{
	const UMetaHumanCharacterEditorHeadAndBodyBlendTool* BlendTool = Cast<UMetaHumanCharacterEditorHeadAndBodyBlendTool>(Tool);
	if (!IsValid(BlendTool))
	{
		return SNullWidget::NullWidget;
	}

	UMetaHumanCharacterEditorHeadAndBodyParameterProperties* BodyParameterProperties = Cast<UMetaHumanCharacterEditorHeadAndBodyParameterProperties>(BlendTool->GetHeadAndBodyParameterProperties());
	UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties* BlendToolProperties = Cast<UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties>(GetToolProperties());
	if (!IsValid(BodyParameterProperties) || !IsValid(BlendToolProperties))
	{
		return SNullWidget::NullWidget;
	}

	FProperty* BlendOptionsProperty = UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties, BodyBlendOptions));
	FProperty* GlobalDeltaProperty = UMetaHumanCharacterEditorHeadAndBodyParameterProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorHeadAndBodyParameterProperties, BodyDelta));

	const void* BlendToolPropertiesDefault = BlendToolProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties>();
	const void* BodyParameterPropertiesDefault = BodyParameterProperties->GetClass()->GetDefaultObject<UMetaHumanCharacterEditorHeadAndBodyParameterProperties>();
	
	return
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("BodyBlendToolBodyParametersSection", "Body Blend Settings"))
		.Visibility(this, &SMetaHumanCharacterEditorBlendToolView::GetBlendSubToolVisibility)
		.Content()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.MinHeight(24.f)
			.Padding(2.f, 0.f)
			.AutoHeight()
			[
				CreatePropertyComboBoxWidget<EBodyBlendOptions>(BlendOptionsProperty->GetDisplayNameText(), BlendToolProperties->BodyBlendOptions, BlendOptionsProperty, BlendToolProperties, BlendToolPropertiesDefault)
			]
			
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreatePropertySpinBoxWidget(LOCTEXT("BodyDelta", "Delta"), GlobalDeltaProperty, BodyParameterProperties, BodyParameterPropertiesDefault)
			]
		];
}

void SMetaHumanCharacterEditorBlendToolView::OnPostEditChangeProperty(FProperty* Property, bool bIsInteractive)
{
	// The base class routes PostEditChangeProperty through GetToolProperties(), which returns the blend tool's
	// primary property set. For properties belonging to HeadAndBodyParameterProperties (e.g. HeadScale,
	// FaceDelta) the notification must instead be dispatched to that object directly.
	if (const UMetaHumanCharacterEditorFaceTool* FaceTool = Cast<UMetaHumanCharacterEditorFaceTool>(Tool))
	{
		if (UMetaHumanCharacterEditorHeadAndBodyParameterProperties* HeadAndBodyParams = FaceTool->GetHeadAndBodyParameterProperties())
		{
			if (Property && Property->GetOwnerClass() == UMetaHumanCharacterEditorHeadAndBodyParameterProperties::StaticClass())
			{
				const EPropertyChangeType::Type ChangeType = bIsInteractive ? EPropertyChangeType::Interactive : EPropertyChangeType::ValueSet;
				FPropertyChangedEvent PropertyChangedEvent(Property, ChangeType);
				HeadAndBodyParams->PostEditChangeProperty(PropertyChangedEvent);

				if (!bIsInteractive && PropertyChangeTransaction.IsValid())
				{
					PropertyChangeTransaction.Reset();
				}
				return;
			}
		}
	}

	Super::OnPostEditChangeProperty(Property, bIsInteractive);
}

bool SMetaHumanCharacterEditorBlendToolView::OnFilterAddAssetDataToAssetView(const FAssetData& AssetData) const
{
	// Do not add fixed body types to body blend asset view
	FName FixedBodyTypePropertyName = GET_MEMBER_NAME_CHECKED(UMetaHumanCharacter, bFixedBodyType);
	bool bFixedBodyType = false;
	AssetData.GetTagValue(FixedBodyTypePropertyName, bFixedBodyType);
	return bFixedBodyType;
}

void SMetaHumanCharacterEditorBlendToolView::OnOverrideItemThumbnailBrush(TSharedPtr<FMetaHumanCharacterAssetViewItem> Item) const
{
	if (!Item.IsValid())
	{
		return;
	}

	if (UTexture2D* Texture = SMetaHumanCharacterEditorAssetViewsPanel::LoadThumbnailAsTextureFromAssetData(Item->AssetData, Item->ThumbnailType))
	{
		Item->ThumbnailImageOverride = FDeferredCleanupSlateBrush::CreateBrush(Texture);
	}
}

EVisibility SMetaHumanCharacterEditorBlendToolView::GetBlendSubToolVisibility() const
{
	UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties* BlendToolProperties = Cast<UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties>(GetToolProperties());
	if (IsValid(BlendToolProperties))
	{
		if (!BlendToolProperties->IsFixedBodyType())
		{
			return EVisibility::Visible;
		}
	}

	return EVisibility::Collapsed;
}

EVisibility SMetaHumanCharacterEditorBlendToolView::GetFixedBodyWarningVisibility() const
{
	UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties* BlendToolProperties = Cast<UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties>(GetToolProperties());
	if (IsValid(BlendToolProperties))
	{
		if (BlendToolProperties->IsFixedBodyType())
		{
			return EVisibility::Visible;
		}
	}

	return EVisibility::Collapsed;
}

FReply SMetaHumanCharacterEditorBlendToolView::OnPerformParametricFitButtonClicked() const
{
	UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties* BlendToolProperties = Cast<UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties>(GetToolProperties());
	if(IsValid(BlendToolProperties))
	{
		BlendToolProperties->PerformParametricFit();
	}

	return FReply::Handled();
}

FReply SMetaHumanCharacterEditorBlendToolView::OnResetNeckButtonClicked() const
{
	if (UMetaHumanCharacterEditorHeadAndBodyBlendTool* BlendTool = Cast<UMetaHumanCharacterEditorHeadAndBodyBlendTool>(Tool))
	{
		BlendTool->ResetFaceNeck();
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
