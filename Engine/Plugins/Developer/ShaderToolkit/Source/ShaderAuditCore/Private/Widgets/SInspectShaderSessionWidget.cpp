// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SInspectShaderSessionWidget.h"
#include "ShaderStatsModel.h"
#include "Widgets/SShaderStatSpreadsheet.h"
#include "Widgets/SShaderAuditOverviewCard.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Styling/CoreStyle.h"

template<typename WidgetType>
static TSharedRef<SWidget> MakeTable(const TCHAR* TableName, TSharedPtr<FShaderAuditSession> InSession)
{
	FSlateFontInfo TitleFont = FCoreStyle::Get().GetFontStyle("DetailsView.CategoryFontStyle");
	TitleFont.Size = 12;

	return SNew(SBorder)
		.Padding(8.0f)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
					.Text(FText::FromString(TableName))
					.Font(TitleFont)
					.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.8f, 0.9f)))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.MaxDesiredHeight(300.0f)
				[
					SNew(SShaderStatSpreadsheet).Model(MakeShared<WidgetType>(InSession))
				]
			]
		];
}

void SInspectShaderSessionWidget::Construct(const FArguments& InArgs)
{
	Session = InArgs._Session;
	RebuildFromSession();
}

void SInspectShaderSessionWidget::SetSession(TSharedPtr<FShaderAuditSession> InSession)
{
	Session = InSession;
	RebuildFromSession();
}

void SInspectShaderSessionWidget::RebuildFromSession()
{
	if (Session.IsValid())
	{
		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SShaderAuditOverviewCard).Session(Session)
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SScrollBox)
				.Orientation(Orient_Vertical)
				+ SScrollBox::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[MakeTable<FMaterialDomainByShaderFrequencyModel>(TEXT("Material Domain"), Session)]
					+ SVerticalBox::Slot().AutoHeight()[MakeTable<FVertexFactoryByShaderFrequencyModel>(TEXT("Vertex Factory by Shader Frequency"), Session)]
					+ SVerticalBox::Slot().AutoHeight()[MakeTable<FVertexFactoryByShaderTypeModel>(TEXT("Vertex Factory by Shader Type"), Session)]
					+ SVerticalBox::Slot().AutoHeight()[MakeTable<FMaterialStatModel>(TEXT("Materials"), Session)]
				]
			]
		];
	}
	else
	{
		ChildSlot
		[
			SNew(STextBlock).Text(NSLOCTEXT("ShaderAudit", "PickFileHint", "Load a shader file first."))
		];
	}
}
