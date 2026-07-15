// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MetaHumanPerformanceMediaSection.h"
#include "Sequencer/MetaHumanPerformanceMovieSceneMediaSection.h"
#include "Sequencer/MetaHumanPerformanceMovieSceneMediaTrack.h"
#include "MetaHumanPerformance.h"
#include "SequencerSectionPainter.h"
#include "Styling/AppStyle.h"
#include "Fonts/FontMeasure.h"


FMetaHumanPerformanceMediaSection::FMetaHumanPerformanceMediaSection(UMovieSceneMediaSection& InSection, TSharedPtr<class FTrackEditorThumbnailPool> InThumbnailPool, TSharedPtr<class ISequencer> InSequencer)
	: FMetaHumanMediaSection{ InSection, InThumbnailPool, InSequencer }
{
}

bool FMetaHumanPerformanceMediaSection::IsReadOnly() const
{
	return true;
}

bool FMetaHumanPerformanceMediaSection::SectionIsResizable() const
{
	return false;
}

int32 FMetaHumanPerformanceMediaSection::OnPaintSection(FSequencerSectionPainter& InPainter) const
{
	// Paint the section as is
	int32 LayerId = FMetaHumanMediaSection::OnPaintSection(InPainter);

	TSharedPtr<ISequencer> Sequencer = SequencerPtr.Pin();
	if (Sequencer.IsValid())
	{
		const UMetaHumanPerformanceMovieSceneMediaSection* MHSection = CastChecked<UMetaHumanPerformanceMovieSceneMediaSection>(Section);
		if (const UMetaHumanPerformance* Performance = MHSection->PerformanceShot)
		{
			LayerId = MetaHumanPerformanceSectionPainterHelper::PaintAnimationResults(InPainter, LayerId, Sequencer.Get(), Section, Performance, false);

			if (Performance->IsProcessing() && Performance->InputType != EDataInputType::Audio && !MHSection->bIsDepthTrack)
			{
				LayerId = MetaHumanPerformanceSectionPainterHelper::PaintCurrentProcessingStage(InPainter, LayerId, Sequencer.Get(), Section, Performance);
			}
		}	
	}

	return LayerId;
}


namespace MetaHumanPerformanceSectionPainterHelper
{

//Using the standard UE5 color values, but the layer bar brush will make them dimmer, exactly as we want
//using an alpha of 0.0f makes the bar disappear for some reason, while 0.004f makes it render with almost-full opacity (??!!)

static const FLinearColor UEColorRed = FLinearColor::FromSRGBColor(FColor::FromHex("#EF353501"));    //239 53 53 0.004  (dimmed by 50%)
static const FLinearColor UEColorYellow = FLinearColor::FromSRGBColor(FColor::FromHex("#FFB80001")); //255 184 0 0.004      -||- 
static const FLinearColor UEColorGreen = FLinearColor::FromSRGBColor(FColor::FromHex("#1FE44B01"));  //31 228 75 0.004      -||- 
static const FLinearColor UEColorGrey = FLinearColor(1.0f, 1.0f, 1.0f, 0.004f); //specifying white to get 50% gray
static const FLinearColor UEColorBlue = FLinearColor(0.0f, 0.0f, 1.0f, 0.004f);
static const FLinearColor UEColorPurple = FLinearColor(1.0f, 0.0f, 1.0f, 0.004f);

static const TMap<EFrameAnimationQuality, FLinearColor> AnimationQualityColors = {
	{ EFrameAnimationQuality::Undefined, UEColorRed },
	{ EFrameAnimationQuality::Preview, UEColorGrey },
	{ EFrameAnimationQuality::Final, UEColorYellow },
	{ EFrameAnimationQuality::PostFiltered, UEColorGreen },
	{ EFrameAnimationQuality::Custom1, UEColorBlue },
	{ EFrameAnimationQuality::Custom2, UEColorPurple },
};

int32 PaintAnimationResults(FSequencerSectionPainter& InPainter, int32 InLayerId, const ISequencer* InSequencer, const UMovieSceneSection* InSection, const UMetaHumanPerformance* InPerformance, bool bInPaintAudioSection)
{
	int32 LayerId = InLayerId + 1;

	const FSlateBrush* SingleFrameBrush = FAppStyle::Get().GetBrush("Sequencer.LayerBar.Background");
	const FFrameRate ProcessingFrameRate = InPerformance->GetFrameRate();
	const FFrameRate SourceRate = ProcessingFrameRate.IsValid() ? ProcessingFrameRate : InSequencer->GetRootDisplayRate();

	const TRange<FFrameNumber> SectionRange = InSection->GetRange();
	const FFrameNumber& SectionStartFrame = SectionRange.GetLowerBoundValue();
	const FFrameNumber& SectionEndFrame = SectionRange.GetUpperBoundValue();
	const FFrameNumber SectionLength = SectionEndFrame - SectionStartFrame;

	const FFrameTime SectionStartFrameSourceRate = FFrameRate::TransformTime(SectionStartFrame, InSequencer->GetRootTickResolution(), SourceRate);
	const int32 SectionStartFrameOffset = InPerformance->GetProcessingLimitFrameRange().GetLowerBoundValue().Value - SectionStartFrameSourceRate.GetFrame().Value;

	const FGeometry& Geometry = InPainter.SectionGeometry;
	const FPaintGeometry PaintGeometry = Geometry.ToPaintGeometry();
	const FVector2f& PaintSize = PaintGeometry.GetLocalSize();

	TArray<TTuple<int32, int32, FLinearColor>> PaintFrameRanges;
	int32 StartFrame = SectionStartFrameOffset;
	FLinearColor RangePaintColor;
	bool bPaintingRange = false;
	bool bContainsData = false;

	for (int32 FrameNumber = 0; FrameNumber < InPerformance->AnimationData.Num(); ++FrameNumber)
	{
		const FFrameAnimationData& AnimationData = InPerformance->AnimationData[FrameNumber];
		bool bShouldPaint = bInPaintAudioSection ? (AnimationData.AudioProcessingMode != EAudioProcessingMode::Undefined) : (AnimationData.AudioProcessingMode != EAudioProcessingMode::FullFace);
		FLinearColor PaintColor = bInPaintAudioSection ? UEColorGreen : AnimationQualityColors[AnimationData.AnimationQuality];

		if (FrameNumber == 0)
		{
			bPaintingRange = bShouldPaint;
			RangePaintColor = PaintColor;
			bContainsData = AnimationData.ContainsData(EFrameAnimationDataType::Any);
		}

		const int32 OffsetFrameNumber = FrameNumber + SectionStartFrameOffset;

		if (bShouldPaint != bPaintingRange || bContainsData != AnimationData.ContainsData(EFrameAnimationDataType::Any) || PaintColor != RangePaintColor)
		{
			// End of range - if it is a painting range then add it
			if(bPaintingRange && bContainsData)
				PaintFrameRanges.Add({StartFrame, OffsetFrameNumber, RangePaintColor});

			// Start new range
			StartFrame = OffsetFrameNumber;
		}

		bPaintingRange = bShouldPaint;
		bContainsData = AnimationData.ContainsData(EFrameAnimationDataType::Any);
		RangePaintColor = PaintColor;
	}

	int32 EndFrame = InPerformance->AnimationData.Num() + SectionStartFrameOffset;
	if (bPaintingRange && bContainsData && (StartFrame != EndFrame))
	{
		PaintFrameRanges.Add({StartFrame, EndFrame, RangePaintColor});
	}

	for (const TTuple<int32, int32, FLinearColor>& PaintFrameRange : PaintFrameRanges)
	{
		FLinearColor Color = PaintFrameRange.Get<2>();
		const FFrameTime StartFrameTime = FFrameRate::TransformTime(FFrameTime{ PaintFrameRange.Get<0>() }, SourceRate, InSequencer->GetRootTickResolution());
		const FFrameTime EndFrameTime = FFrameRate::TransformTime(FFrameTime{ PaintFrameRange.Get<1>() }, SourceRate, InSequencer->GetRootTickResolution());

		const float StartFramePosition = PaintSize.X * StartFrameTime.FrameNumber.Value / SectionLength.Value;
		const float EndFramePosition = PaintSize.X * EndFrameTime.FrameNumber.Value / SectionLength.Value;
		const float PaintFrameSize = EndFramePosition - StartFramePosition;

		float FullHeight = PaintSize.Y;
		float PegMarginHeight = FullHeight * 0.33f;        //magic numbers arrived at by visually assessing the relation of peg hole height to the entire section height
		float ProgressIndicatorHeight = FullHeight * 0.47f; //make the height of the progress indicator such that it leaves a approximately a pixel before the bottom peg hole line
		float YOffset = PegMarginHeight * 1.1f;              //start drawing the progress indicator about a pixel below the upper peg hole line

		FSlateDrawElement::MakeBox(
			InPainter.DrawElements,
			LayerId,
			InPainter.SectionGeometry.ToPaintGeometry(
				FVector2f{ PaintFrameSize, ProgressIndicatorHeight}, FSlateLayoutTransform(FVector2f{ StartFramePosition, YOffset })),
			SingleFrameBrush,
			ESlateDrawEffect::InvertAlpha,
			Color
		);
	}

	return LayerId;
}

int32 PaintCurrentProcessingStage(FSequencerSectionPainter& InPainter, int32 InLayerId, const ISequencer* InSequencer, const UMovieSceneSection* InSection, const UMetaHumanPerformance* InPerformance)
{
	int32 LayerId = InLayerId + 100; // Ensuring it is always on top

	int32 PipelineStage = (InPerformance->GetPipelineStage() + 1) + (InPerformance->GetPipelineFrameRange() * InPerformance->GetTotalPipelineStage());
	int32 TotalPipelineStage = InPerformance->GetTotalPipelineStage() * InPerformance->GetPipelineFrameRanges();

	const FString CustomText = FString::Printf(TEXT("Processing stage %d of %d"), PipelineStage, TotalPipelineStage);

	// Most of the code is taken from how MediaFramework draws text on the Media Track (FMediaThumbnailSection::DrawMediaInfo)
	const FSlateFontInfo FontInfo = FAppStyle::GetFontStyle("SmallFont");
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FVector2f TextSize = FontMeasure->Measure(CustomText, FontInfo);
	FVector2D TextOffset(EForceInit::ForceInitToZero);
	FMargin ContentPadding = FMargin(8.0f, 15.0f);

	const float AvailableSectionHeight = InPainter.SectionGeometry.Size.Y - 9.0f;
	float FontHeight = FontInfo.Size + 4.f;

	constexpr float TextYOffsetFromTop = 13.0f;

	const FVector2f SectionSize = InPainter.SectionGeometry.GetLocalSize();

	FVector2D TopLeft = InPainter.SectionGeometry.AbsoluteToLocal(InPainter.SectionClippingRect.GetTopLeft());
	TextOffset.X = SectionSize.X - TextSize.X - ContentPadding.Right;
	TextOffset.Y += TextYOffsetFromTop;

	FColor TextColor(220, 220, 220);
	TextColor.A = static_cast<uint8>(InPainter.GhostAlpha * 255);

	FSlateDrawElement::MakeText(
		InPainter.DrawElements,
		LayerId,
		InPainter.SectionGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(TextOffset + FVector2D(1.f, 1.f))),
		CustomText,
		FontInfo,
		ESlateDrawEffect::None,
		FLinearColor(0, 0, 0, .5f * InPainter.GhostAlpha)
	);

	FSlateDrawElement::MakeText(
		InPainter.DrawElements,
		LayerId,
		InPainter.SectionGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(TextOffset)),
		CustomText,
		FontInfo,
		ESlateDrawEffect::None,
		TextColor
	);

	// Returning next layer for the next user
	return InLayerId + 1;
}

}