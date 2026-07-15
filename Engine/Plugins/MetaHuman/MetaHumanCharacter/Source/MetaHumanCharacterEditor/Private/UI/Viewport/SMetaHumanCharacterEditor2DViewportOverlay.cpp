// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditor2DViewportOverlay.h"

#include "MetaHumanCharacterEditorStyle.h"
#include "Styling/SlateBrush.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SScaleBox.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCharacterEditor2DViewportOverlay"

namespace UE::MetaHuman::Private
{
	static const FName DefaultOverlayBrushName = TEXT("MetaHumanCharacterEditorTools.Rounded.Transparency.DefaultBrush");
	static const FName HoveredOverlayBrushName = TEXT("MetaHumanCharacterEditorTools.Rounded.DefaultBrush");
	
	static constexpr float DefaultOverlayWidth = 300.f;
	static constexpr float DefaultOverlayHeight = 200.f;
	static constexpr float HoveredMultiplier = 1.01f;
}

void SMetaHumanCharacterEditor2DViewportOverlay::Construct(const FArguments& InArgs)
{
	ChildSlot
		[
			SNew(SBox)
			.WidthOverride_Lambda([this]()
				{
					using namespace UE::MetaHuman::Private;

					if (OverlayBorder.IsValid() && OverlayBorder->IsHovered())
					{
						return DefaultOverlayWidth * HoveredMultiplier;
					}
					return DefaultOverlayWidth;
				})
			.HeightOverride_Lambda([this]()
				{
					using namespace UE::MetaHuman::Private;

					if (OverlayBorder.IsValid() && OverlayBorder->IsHovered())
					{
						return DefaultOverlayHeight * HoveredMultiplier;
					}
					return DefaultOverlayHeight;
				})
			.Clipping(EWidgetClipping::ClipToBounds)
			[
				SAssignNew(OverlayBorder, SBorder)
				.BorderImage(this, &SMetaHumanCharacterEditor2DViewportOverlay::GetOverlayBorderImageBrush)
				[
					SNew(SVerticalBox)

					// Label section
					+ SVerticalBox::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					.Padding(4.f, 2.f)
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.Padding(2.f)
						.AutoWidth()
						[
							SNew(SImage)
							.Image(FMetaHumanCharacterEditorStyle::Get().GetBrush("Viewport.Icons.Camera"))
						]

						+ SHorizontalBox::Slot()
						.Padding(4.f, 3.f, 2.f, 2.f)
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(InArgs._Label)
							.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						]
					]

					+ SVerticalBox::Slot()
					.Padding(4.f, 0.f)
					.AutoHeight()
					[
						SNew(SSeparator)
						.Orientation(EOrientation::Orient_Horizontal)
						.Thickness(.4f)
						.SeparatorImage(FMetaHumanCharacterEditorStyle::Get().GetBrush("MetaHumanCharacterEditorTools.WhiteBrush"))
						.ColorAndOpacity(FLinearColor(.6f, .6f, .6f, 1.f))
					]

					// Image Brush section
					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					.Padding(10.f)
					[
						SNew(SScaleBox)
						.Stretch(EStretch::ScaleToFill)
						[
							SNew(SImage)
							.Image(InArgs._ImageBrush)
							.OnMouseButtonDown(InArgs._OnMouseButtonDown)
						]
					]
				]
			]
		];
}

const FSlateBrush* SMetaHumanCharacterEditor2DViewportOverlay::GetOverlayBorderImageBrush() const
{
	using namespace UE::MetaHuman::Private;

	if (OverlayBorder.IsValid() && OverlayBorder->IsHovered())
	{
		return FMetaHumanCharacterEditorStyle::Get().GetBrush(HoveredOverlayBrushName);
	}
	return FMetaHumanCharacterEditorStyle::Get().GetBrush(DefaultOverlayBrushName);
}	

#undef LOCTEXT_NAMESPACE
