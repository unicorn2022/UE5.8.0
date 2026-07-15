// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanPerformanceCustomizations.h"
#include "MetaHumanPerformance.h"
#include "MetaHumanPerformanceLog.h"
#include "SMetaHumanCameraCombo.h"
#include "FrameRangeArrayBuilder.h"
#include "AudioDrivenAnimationMood.h"

#include "DetailLayoutBuilder.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Misc/MessageDialog.h"
#include "PropertyRestriction.h"

#include "IDetailGroup.h"

#define LOCTEXT_NAMESPACE "MetaHumanPerformance"

namespace UE::MetaHuman::Private
{

void AddFocalLengthProperty(IDetailLayoutBuilder& InDetailBuilder, IDetailGroup& InFacialTrackingGroup, UMetaHumanPerformance* InPerformance)
{
	TSharedRef<IPropertyHandle> FocalLengthProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, FocalLength));

	IDetailPropertyRow& FocalLengthRow = InFacialTrackingGroup.AddPropertyRow(FocalLengthProperty);

	TSharedPtr<SWidget> NameWidget;
	TSharedPtr<SWidget> ValueWidget;
	FocalLengthRow.GetDefaultWidgets(NameWidget, ValueWidget);

	FocalLengthRow.CustomWidget()
		.NameContent()
		[
			NameWidget.ToSharedRef()
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 5, 0, 0)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ToolTipText_Lambda([InPerformance]()
				{
					if (InPerformance->FocalLength < 0)
					{
						return LOCTEXT("FocalNotSetTooltip", "Focal length is set when the \"Estimate\" button is pressed");
					}
					else
					{
						return FText::FromString(FString::Printf(TEXT("%.2f pixels"), InPerformance->FocalLength));
					}
				})
				.Text_Lambda([InPerformance]()
				{
					if (InPerformance->FocalLength < 0)
					{ 
						return LOCTEXT("FocalNotSet", "Not Set");
					}
					else
					{
						return FText::FromString(FString::Printf(TEXT("%.2f px"), InPerformance->FocalLength));
					}
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(10, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("FocalEstimate", "Estimate"))
				.IsEnabled_Lambda([InPerformance]()
				{
					return InPerformance->CanProcess();
				})
				.OnClicked_Lambda([InPerformance]()
				{
					FString ErrorMessage;
					if (!InPerformance->EstimateFocalLength(ErrorMessage))
					{
						FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("Failed to estimate focal length:\n%s"), *ErrorMessage)));
					}

					return FReply::Handled();
				})
			]
		];
}

void AddHeadPoseCalibrationProperties(UMetaHumanPerformance* InPerformance, 
									  IDetailPropertyRow& InOutNeutralPoseCalibrationEnabledRow,
									  IDetailPropertyRow& InOutNeutralPoseCalibrationFrameRow,
									  IDetailPropertyRow& InOutNeutralPoseCalibrationAlphaRow,
									  IDetailPropertyRow& InOutNeutralPoseCalibrationCurvesRow)
{
	if (InPerformance->InputType == EDataInputType::MonoFootage)
	{
		InOutNeutralPoseCalibrationEnabledRow.EditCondition(true, {});
		InOutNeutralPoseCalibrationFrameRow.EditCondition(TAttribute<bool>::CreateLambda([InPerformance]
		{
			return InPerformance->bNeutralPoseCalibrationEnabled;
		}), {});
		InOutNeutralPoseCalibrationAlphaRow.EditCondition(TAttribute<bool>::CreateLambda([InPerformance]
		{
			return InPerformance->bNeutralPoseCalibrationEnabled;
		}), {});
		InOutNeutralPoseCalibrationCurvesRow.EditCondition(TAttribute<bool>::CreateLambda([InPerformance]
		{
			return InPerformance->bNeutralPoseCalibrationEnabled;
		}), {});

		InOutNeutralPoseCalibrationEnabledRow.EditConditionHides(false);
		InOutNeutralPoseCalibrationFrameRow.EditConditionHides(false);
		InOutNeutralPoseCalibrationAlphaRow.EditConditionHides(false);
		InOutNeutralPoseCalibrationCurvesRow.EditConditionHides(false);
	}
	else
	{
		InOutNeutralPoseCalibrationEnabledRow.EditCondition(false, {});
		InOutNeutralPoseCalibrationFrameRow.EditCondition(false, {});
		InOutNeutralPoseCalibrationAlphaRow.EditCondition(false, {});
		InOutNeutralPoseCalibrationCurvesRow.EditCondition(false, {});

		InOutNeutralPoseCalibrationEnabledRow.EditConditionHides(true);
		InOutNeutralPoseCalibrationFrameRow.EditConditionHides(true);
		InOutNeutralPoseCalibrationAlphaRow.EditConditionHides(true);
		InOutNeutralPoseCalibrationCurvesRow.EditConditionHides(true);
	}
}

void AddHeadMovementProperties(UMetaHumanPerformance* InPerformance,
							   IDetailPropertyRow& InOutAutoChooseReferenceFrameRow,
							   IDetailPropertyRow& InOutHeadMovementReferenceFrameRow)
{
	if (InPerformance->InputType == EDataInputType::DepthFootage || InPerformance->InputType == EDataInputType::MonoFootage)
	{
		InOutAutoChooseReferenceFrameRow.EditCondition(TAttribute<bool>::CreateLambda([] { return true; }), {});
		InOutHeadMovementReferenceFrameRow.EditCondition(TAttribute<bool>::CreateLambda([InPerformance] { return !InPerformance->bAutoChooseHeadMovementReferenceFrame; }), {});

		InOutAutoChooseReferenceFrameRow.EditConditionHides(false);
		InOutHeadMovementReferenceFrameRow.EditConditionHides(false);
	}
	else
	{
		InOutAutoChooseReferenceFrameRow.EditCondition(TAttribute<bool>::CreateLambda([] { return false; }), {});
		InOutHeadMovementReferenceFrameRow.EditCondition(TAttribute<bool>::CreateLambda([] { return false; }), {});

		InOutAutoChooseReferenceFrameRow.EditConditionHides(true);
		InOutHeadMovementReferenceFrameRow.EditConditionHides(true);
	}
}

template<typename FCheckLambda>
void AddInvertedBooleanProperty(UMetaHumanPerformance* InPerformance, TSharedRef<IPropertyHandle> InBooleanProperty, IDetailPropertyRow& InOutBooleanRow, FCheckLambda InCheckLambda)
{
	TSharedPtr<SWidget> NameWidget;
	TSharedPtr<SWidget> ValueWidget;

	InOutBooleanRow.GetDefaultWidgets(NameWidget, ValueWidget);

	InOutBooleanRow.CustomWidget()
		.NameContent()
		[
			NameWidget.ToSharedRef()
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		.MaxDesiredWidth(0.0f)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([InPerformance, CheckLambda = MoveTemp(InCheckLambda)]()
			{
				return CheckLambda() ? ECheckBoxState::Unchecked : ECheckBoxState::Checked;
			})
			.OnCheckStateChanged_Lambda([InBooleanProperty](ECheckBoxState InState)
			{
				InBooleanProperty->SetValue(InState != ECheckBoxState::Checked);
			})
			.IsEnabled_Lambda([InPerformance, InBooleanProperty]()
			{
				return InPerformance && InPerformance->CanEditChange(InBooleanProperty->GetProperty());
			})
			.ToolTipText_Lambda([InBooleanProperty]()
			{
				FText OutText;
				InBooleanProperty->GetValueAsDisplayText(OutText);
				const FText Inverted = OutText.EqualTo(FText::FromString(TEXT("True")))
					? FText::FromString(TEXT("False"))
					: FText::FromString(TEXT("True"));

				return Inverted;
			})
		];
}

}

TSharedRef<IDetailCustomization> FMetaHumanPerformanceCustomization::MakeInstance()
{
	return MakeShared<FMetaHumanPerformanceCustomization>();
}

void FMetaHumanPerformanceCustomization::CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder)
{
	using namespace UE::MetaHuman::Private;

	UMetaHumanPerformance* Performance = nullptr;

	// Get the performance object that we're building the details panel for.
	if (!InDetailBuilder.GetSelectedObjects().IsEmpty())
	{
		Performance = Cast<UMetaHumanPerformance>(InDetailBuilder.GetSelectedObjects()[0].Get());
	}
	else
	{
		return;
	}

	TSharedPtr<SWidget> NameWidget;
	TSharedPtr<SWidget> ValueWidget;

	TSharedRef<IPropertyHandle> CameraProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, Camera));
	IDetailPropertyRow* CameraRow = InDetailBuilder.EditDefaultProperty(CameraProperty);
	check(CameraRow);

	CameraRow->GetDefaultWidgets(NameWidget, ValueWidget);

	TSharedRef<SMetaHumanCameraCombo> CameraCombo = SNew(SMetaHumanCameraCombo, &Performance->CameraNames, &Performance->Camera, Performance, CameraProperty.ToSharedPtr());
	Performance->OnSourceDataChanged().AddSP(CameraCombo, &SMetaHumanCameraCombo::HandleSourceDataChanged);

	CameraRow->CustomWidget()
		.NameContent()
		[
			NameWidget.ToSharedRef()
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		.MaxDesiredWidth(0.0f)
		[
			CameraCombo
		];

	TSharedRef<IPropertyHandle> SolveTypeHandle =
		InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, SolveType));

	// disable chin compress if there is no config for this
	const bool bDisableChinCompress = !Performance->HasSolverHierarchicalDefinitionsPlusChinCompressConfig();
	if (bDisableChinCompress)
	{
		uint8 SolveTypeValue = 0;
		if (SolveTypeHandle->GetValue(SolveTypeValue) == FPropertyAccess::Success)
		{
			const UEnum* Enum = StaticEnum<ESolveType>();
			if (static_cast<ESolveType>(SolveTypeValue) == ESolveType::AdditionalTweakersPlusChinCompress)
			{
				SolveTypeHandle->SetValue(static_cast<uint8>(ESolveType::AdditionalTweakers));
			}
		}

		TSharedRef<FPropertyRestriction> Restriction =
			MakeShared<FPropertyRestriction>(
				NSLOCTEXT("MetaHumanPerformance", "SolveTypeRestriction", "Additional Tweakers plus Chin Compress is not a valid option for this config")
			);

		Restriction->AddDisabledValue(TEXT("AdditionalTweakersPlusChinCompress"));
		SolveTypeHandle->AddRestriction(Restriction);
	}

	TSharedRef<IPropertyHandle> SkipPreviewProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, bSkipPreview));

	if (Performance->InputType != EDataInputType::MonoFootage)
	{
		IDetailPropertyRow* SkipPreviewRow = InDetailBuilder.EditDefaultProperty(SkipPreviewProperty);
		check(SkipPreviewRow);

		AddInvertedBooleanProperty(Performance, SkipPreviewProperty, *SkipPreviewRow, [Performance]()
								   {
									   return Performance && Performance->bSkipPreview && Performance->SolveType != ESolveType::Preview;
								   });
	}

	TSharedRef<IPropertyHandle> SkipFilteringProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, bSkipFiltering));
	IDetailPropertyRow* SkipFilteringRow = InDetailBuilder.EditDefaultProperty(SkipFilteringProperty);
	check(SkipFilteringRow);

	AddInvertedBooleanProperty(Performance, SkipFilteringProperty, *SkipFilteringRow, [Performance]()
							   {
								   return Performance && Performance->bSkipFiltering && Performance->FootageCaptureData;
							   });

	TSharedRef<IPropertyHandle> SkipTongueSolveProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, bSkipTongueSolve));

	if (Performance->InputType != EDataInputType::MonoFootage)
	{
		IDetailPropertyRow* SkipTongueSolveRow = InDetailBuilder.EditDefaultProperty(SkipTongueSolveProperty);
		check(SkipTongueSolveRow);

		AddInvertedBooleanProperty(Performance, SkipTongueSolveProperty, *SkipTongueSolveRow, [Performance]()
								   {
									   return Performance && Performance->bSkipTongueSolve && Performance->GetAudioForProcessing();
								   });
	}

	TSharedRef<IPropertyHandle> SkipPerVertexSolveProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, bSkipPerVertexSolve));
	IDetailPropertyRow* SkipPerVertexSolveRow = InDetailBuilder.EditDefaultProperty(SkipPerVertexSolveProperty);
	check(SkipPerVertexSolveRow);

	AddInvertedBooleanProperty(Performance, SkipPerVertexSolveProperty, *SkipPerVertexSolveRow, [Performance]()
							   {
								   return Performance && Performance->bSkipPerVertexSolve && Performance->FootageCaptureData;
							   });

	TSharedRef<IPropertyHandle> SkipDiagnosticsProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, bSkipDiagnostics));
	IDetailPropertyRow* SkipDiagnosticsRow = InDetailBuilder.EditDefaultProperty(SkipDiagnosticsProperty);
	check(SkipDiagnosticsRow);

	AddInvertedBooleanProperty(Performance, SkipDiagnosticsProperty, *SkipDiagnosticsRow, [Performance]()
							   {
								   return Performance && Performance->bSkipDiagnostics && Performance->FootageCaptureData;
							   });

	TSharedRef<IPropertyHandle> UserExcludedFramesProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, UserExcludedFrames));
	TSharedRef<IPropertyHandle> ProcessingExcludedFramesProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, ProcessingExcludedFrames));

	IDetailCategoryBuilder& ExcludedFramesCategory = InDetailBuilder.EditCategory(UserExcludedFramesProperty->GetDefaultCategoryName());

	ExcludedFramesCategory.AddCustomBuilder(MakeShareable(new FFrameRangeArrayBuilder(UserExcludedFramesProperty, Performance->UserExcludedFrames, &Performance->OnGetCurrentFrame())), false);
	ExcludedFramesCategory.AddCustomBuilder(MakeShareable(new FFrameRangeArrayBuilder(ProcessingExcludedFramesProperty, Performance->ProcessingExcludedFrames)), false);

	bool bShowAudioChannelDetail = Performance->InputType == EDataInputType::Audio && !Performance->bRealtimeAudio;
	TSharedRef<IPropertyHandle> AudioChannelProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, AudioChannelIndex));
	IDetailPropertyRow* AudioChannelRow = InDetailBuilder.EditDefaultProperty(AudioChannelProperty);

	if (bShowAudioChannelDetail)
	{
		AudioChannelRow->EditCondition(TAttribute<bool>::CreateLambda([Performance] { return !Performance->bDownmixChannels; }), {});
		AudioChannelRow->EditConditionHides(false);
	}
	else
	{
		AudioChannelRow->EditCondition(TAttribute<bool>::CreateLambda([] { return false; }), {});
		AudioChannelRow->EditConditionHides(true);
	}

	TSharedRef<IPropertyHandle> RealtimeAudioMoodProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, RealtimeAudioMood));
	IDetailPropertyRow* RealtimeAudioMoodRow = InDetailBuilder.EditDefaultProperty(RealtimeAudioMoodProperty);
	check(RealtimeAudioMoodRow);

	RealtimeAudioMoodRow->GetDefaultWidgets(NameWidget, ValueWidget);

	RealtimeAudioMoodRow->CustomWidget()
		.NameContent()
		[
			NameWidget.ToSharedRef()
		]
		.ValueContent()
		[
			SNew(SAudioDrivenAnimationMood, false, RealtimeAudioMoodProperty)
		];

	IDetailCategoryBuilder& DataCategory = InDetailBuilder.EditCategory(TEXT("Data"));
	IDetailCategoryBuilder& VisualizationCategory = InDetailBuilder.EditCategory(TEXT("Visualization"));
	IDetailCategoryBuilder& ProcessingCategory = InDetailBuilder.EditCategory(TEXT("Processing Parameters"));
	IDetailCategoryBuilder& DiagnosticsCategory = InDetailBuilder.EditCategory(TEXT("Processing Diagnostics"));

	DataCategory.SetSortOrder(1000);
	VisualizationCategory.SetSortOrder(1001);
	ProcessingCategory.SetSortOrder(1002);
	ExcludedFramesCategory.SetSortOrder(1003);
	DiagnosticsCategory.SetSortOrder(1004);

	TSharedRef<IPropertyHandle> AutoChooseReferenceFrameProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, bAutoChooseHeadMovementReferenceFrame));
	TSharedRef<IPropertyHandle> HeadMovementReferenceFrameProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, HeadMovementReferenceFrame));

	TSharedRef<IPropertyHandle> NeutralPoseCalibrationEnabledProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, bNeutralPoseCalibrationEnabled));
	TSharedRef<IPropertyHandle> NeutralPoseCalibrationFrameProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, NeutralPoseCalibrationFrame));
	TSharedRef<IPropertyHandle> NeutralPoseCalibrationAlphaProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, NeutralPoseCalibrationAlpha));
	TSharedRef<IPropertyHandle> NeutralPoseCalibrationCurvesProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, NeutralPoseCalibrationCurves));

	TSharedRef<IPropertyHandle> FaceTrackingProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, bFaceTracking));
	TSharedRef<IPropertyHandle> BodyTrackingProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, bBodyTracking));
	TSharedRef<IPropertyHandle> BodyHeightProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, BodyHeight));

	InDetailBuilder.HideProperty(FaceTrackingProperty);
	InDetailBuilder.HideProperty(BodyTrackingProperty);

	if (Performance->InputType == EDataInputType::MonoFootage)
	{
		// Face
		IDetailGroup& FacialTrackingGroup = ProcessingCategory.AddGroup(TEXT("FacialTracking"), LOCTEXT("FacialTracking", "Facial Tracking"), false, true);

		FacialTrackingGroup.HeaderRow()
			.NameContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([FaceTrackingProperty]
					{
						bool bValue = false;
						FaceTrackingProperty->GetValue(bValue);
						return bValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([FaceTrackingProperty, &FacialTrackingGroup](ECheckBoxState NewState)
					{
						FacialTrackingGroup.ToggleExpansion(NewState == ECheckBoxState::Checked);
						FaceTrackingProperty->SetValue(NewState == ECheckBoxState::Checked);
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 0, 0, 0)
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
					.Text(LOCTEXT("FacialTracking", "Facial Tracking"))
				]
			];

		IDetailPropertyRow& SkipPreviewRow = FacialTrackingGroup.AddPropertyRow(SkipPreviewProperty);
		AddInvertedBooleanProperty(Performance, SkipPreviewProperty, SkipPreviewRow, [Performance]()
								   {
									   return Performance && Performance->bSkipPreview && Performance->SolveType != ESolveType::Preview;
								   });

		IDetailPropertyRow& SkipTongueSolveRow = FacialTrackingGroup.AddPropertyRow(SkipTongueSolveProperty);
		AddInvertedBooleanProperty(Performance, SkipTongueSolveProperty, SkipTongueSolveRow, [Performance]()
								   {
									   return Performance && Performance->bSkipTongueSolve && Performance->GetAudioForProcessing();
								   });

		IDetailPropertyRow& AutoChooseReferenceFrameRow = FacialTrackingGroup.AddPropertyRow(AutoChooseReferenceFrameProperty);
		IDetailPropertyRow& HeadMovementReferenceFrameRow = FacialTrackingGroup.AddPropertyRow(HeadMovementReferenceFrameProperty);
		
		UE::MetaHuman::Private::AddHeadMovementProperties(Performance, AutoChooseReferenceFrameRow, HeadMovementReferenceFrameRow);

		IDetailPropertyRow& NeutralPoseCalibrationEnabledRow = FacialTrackingGroup.AddPropertyRow(NeutralPoseCalibrationEnabledProperty);
		IDetailPropertyRow& NeutralPoseCalibrationFrameRow = FacialTrackingGroup.AddPropertyRow(NeutralPoseCalibrationFrameProperty);
		IDetailPropertyRow& NeutralPoseCalibrationAlphaRow = FacialTrackingGroup.AddPropertyRow(NeutralPoseCalibrationAlphaProperty);
		IDetailPropertyRow& NeutralPoseCalibrationCurvesRow = FacialTrackingGroup.AddPropertyRow(NeutralPoseCalibrationCurvesProperty);

		UE::MetaHuman::Private::AddHeadPoseCalibrationProperties(Performance,
																 NeutralPoseCalibrationEnabledRow,
																 NeutralPoseCalibrationFrameRow,
																 NeutralPoseCalibrationAlphaRow,
																 NeutralPoseCalibrationCurvesRow);

		FacialTrackingGroup.AddPropertyRow(
			InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, HeadMovementMode)));

		FacialTrackingGroup.AddPropertyRow(
			InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, bHeadStabilization)));

		FacialTrackingGroup.AddPropertyRow(
			InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, MonoSmoothingParams)));

		UE::MetaHuman::Private::AddFocalLengthProperty(InDetailBuilder, FacialTrackingGroup, Performance);

		IDetailGroup& AdvancedGroup = FacialTrackingGroup.AddGroup(
			TEXT("Advanced"), LOCTEXT("Advanced", "Advanced"), false);

		AdvancedGroup.AddPropertyRow(
			InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, HeadAllowedRotationLeftRight)));

		AdvancedGroup.AddPropertyRow(
			InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, HeadAllowedRotationUpDown)));

		AdvancedGroup.AddPropertyRow(
			InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, HeadRotationHandler)));

		AdvancedGroup.AddPropertyRow(
			InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, MonocularAnimationPipelineModels)));

		if (IModularFeatures::Get().IsModularFeatureAvailable(IMetaHumanBodyTrackerInterface::GetModularFeatureName()))
		{
			IDetailGroup& BodyTrackingGroup = ProcessingCategory.AddGroup(TEXT("BodyTracking"), LOCTEXT("BodyTracking", "Body Tracking"), false, true);
			BodyTrackingGroup.HeaderRow()
				.NameContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([BodyTrackingProperty]
						{
							bool bValue = false;
							BodyTrackingProperty->GetValue(bValue);
							return bValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([BodyTrackingProperty, &BodyTrackingGroup](ECheckBoxState NewState)
						{
							BodyTrackingGroup.ToggleExpansion(NewState == ECheckBoxState::Checked);
							BodyTrackingProperty->SetValue(NewState == ECheckBoxState::Checked);
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4, 0, 0, 0)
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFontBold())
						.Text(LOCTEXT("BodyTracking", "Body Tracking"))
					]
				];
			TSharedRef<IPropertyHandle> AutoBodyHeightProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, bAutoBodyHeight));
			

			IDetailPropertyRow& AutoBodyHeightRow = BodyTrackingGroup.AddPropertyRow(AutoBodyHeightProperty);
			IDetailPropertyRow& BodyHeightRow = BodyTrackingGroup.AddPropertyRow(BodyHeightProperty);
			BodyHeightRow.EditCondition(TAttribute<bool>::CreateLambda([Performance] { return !Performance->bAutoBodyHeight; }), {});
			BodyHeightRow.EditConditionHides(false);

			BodyTrackingGroup.AddPropertyRow(
				InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, bEnableFootLocking)));

			IDetailGroup& BodyAdvancedGroup = BodyTrackingGroup.AddGroup(
				TEXT("Advanced"), LOCTEXT("Advanced", "Advanced"), false);

			BodyAdvancedGroup.AddPropertyRow(
				InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, BodyDetectionConfidence)));

			BodyAdvancedGroup.AddPropertyRow(
				InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, BodyTrackingConfidence)));


		}
	}
	else
	{
		IDetailPropertyRow* AutoChooseReferenceFrameRow = InDetailBuilder.EditDefaultProperty(AutoChooseReferenceFrameProperty);
		check(AutoChooseReferenceFrameRow);

		IDetailPropertyRow* HeadMovementReferenceFrameRow = InDetailBuilder.EditDefaultProperty(HeadMovementReferenceFrameProperty);
		check(HeadMovementReferenceFrameRow);

		UE::MetaHuman::Private::AddHeadMovementProperties(Performance,
														  *AutoChooseReferenceFrameRow,
														  *HeadMovementReferenceFrameRow);

		IDetailPropertyRow* NeutralPoseCalibrationEnabledRow = InDetailBuilder.EditDefaultProperty(NeutralPoseCalibrationEnabledProperty);
		check(NeutralPoseCalibrationEnabledRow);
		IDetailPropertyRow* NeutralPoseCalibrationFrameRow = InDetailBuilder.EditDefaultProperty(NeutralPoseCalibrationFrameProperty);
		check(NeutralPoseCalibrationFrameRow);
		IDetailPropertyRow* NeutralPoseCalibrationAlphaRow = InDetailBuilder.EditDefaultProperty(NeutralPoseCalibrationAlphaProperty);
		check(NeutralPoseCalibrationAlphaRow);
		IDetailPropertyRow* NeutralPoseCalibrationCurvesRow = InDetailBuilder.EditDefaultProperty(NeutralPoseCalibrationCurvesProperty);
		check(NeutralPoseCalibrationCurvesRow);

		UE::MetaHuman::Private::AddHeadPoseCalibrationProperties(Performance,
																 *NeutralPoseCalibrationEnabledRow,
																 *NeutralPoseCalibrationFrameRow,
																 *NeutralPoseCalibrationAlphaRow,
																 *NeutralPoseCalibrationCurvesRow);

		IDetailPropertyRow* BodyHeightRow = InDetailBuilder.EditDefaultProperty(BodyHeightProperty);
		check(BodyHeightRow);
		BodyHeightRow->EditCondition(false, {});
		BodyHeightRow->EditConditionHides(true);
	}

	TSharedRef<IPropertyHandle> ShowSkeletonProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, bShowSkeleton));
	IDetailPropertyRow* ShowSkeletonRow = InDetailBuilder.EditDefaultProperty(ShowSkeletonProperty);
	check(ShowSkeletonRow);

	TSharedRef<IPropertyHandle> SkeletonOffsetProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, SkeletonOffset));
	IDetailPropertyRow* SkeletonOffsetRow = InDetailBuilder.EditDefaultProperty(SkeletonOffsetProperty);
	check(SkeletonOffsetRow);

	TSharedRef<IPropertyHandle> SkeletonColorProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, SkeletonColor));
	IDetailPropertyRow* SkeletonColorRow = InDetailBuilder.EditDefaultProperty(SkeletonColorProperty);
	check(SkeletonColorRow);

	ShowSkeletonRow->Visibility(TAttribute<EVisibility>::CreateLambda([Performance]
		{
			return Performance->InputType == EDataInputType::MonoFootage && Performance->bBodyTracking && IModularFeatures::Get().IsModularFeatureAvailable(IMetaHumanBodyTrackerInterface::GetModularFeatureName()) ? EVisibility::Visible : EVisibility::Collapsed;
		}));

	SkeletonOffsetRow->Visibility(TAttribute<EVisibility>::CreateLambda([Performance]
		{
			return Performance->InputType == EDataInputType::MonoFootage && Performance->bBodyTracking && IModularFeatures::Get().IsModularFeatureAvailable(IMetaHumanBodyTrackerInterface::GetModularFeatureName()) ? EVisibility::Visible : EVisibility::Collapsed;
		}));

	SkeletonColorRow->Visibility(TAttribute<EVisibility>::CreateLambda([Performance]
		{
			return Performance->InputType == EDataInputType::MonoFootage && Performance->bBodyTracking && IModularFeatures::Get().IsModularFeatureAvailable(IMetaHumanBodyTrackerInterface::GetModularFeatureName()) ? EVisibility::Visible : EVisibility::Collapsed;
		}));

	if (IModularFeatures::Get().IsModularFeatureAvailable(IMetaHumanBodyTrackerInterface::GetModularFeatureName()))
	{
		IMetaHumanBodyTrackerInterface& BodyTracker = IModularFeatures::Get().GetModularFeature<IMetaHumanBodyTrackerInterface>(IMetaHumanBodyTrackerInterface::GetModularFeatureName());

		BodyTracker.CustomizePerformanceDetails(InDetailBuilder);
	}
}

#undef LOCTEXT_NAMESPACE
