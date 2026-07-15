// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SShaderAuditOverviewCard.h"
#include "ShaderAuditSession.h"
#include "ShaderAuditTypes.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "ShaderAuditOverviewCard"

void SShaderAuditOverviewCard::Construct(const FArguments& InArgs)
{
	TSharedPtr<FShaderAuditSession> Session = InArgs._Session;
	if (!Session.IsValid())
	{
		return;
	}

	// Accumulate stats in a single pass over UniqueShaders
	struct FFreqStats
	{
		int32 Count = 0;
		uint64 Size = 0;
	};
	TMap<FName, FFreqStats> FrequencyStats;
	uint64 UniqueBytes = 0;
	uint64 DiskBytes = 0;
	bool bHasSize = false;

	for (const FShaderAuditSession::FUniqueShader& Shader : Session->UniqueShaders)
	{
		if (Shader.CompressedSize > 0)
		{
			bHasSize = true;
		}
		UniqueBytes += Shader.CompressedSize;
		DiskBytes += (uint64)Shader.CompressedSize * FMath::Max<uint16>(Shader.ArchiveCount, 1);

		if (Shader.FirstEntryIdx != INDEX_NONE)
		{
			const FName Frequency = Session->StableShaderKeyAndValueArray[Shader.FirstEntryIdx].TargetFrequency;
			FFreqStats& Stats = FrequencyStats.FindOrAdd(Frequency);
			Stats.Count++;
			Stats.Size += Shader.CompressedSize;
		}
	}

	// Sort frequencies by size desc, count desc, then lexical
	TArray<FName> SortedFrequencies;
	FrequencyStats.GetKeys(SortedFrequencies);
	SortedFrequencies.Sort([&FrequencyStats](const FName& A, const FName& B)
	{
		const FFreqStats& SA = FrequencyStats.FindChecked(A);
		const FFreqStats& SB = FrequencyStats.FindChecked(B);
		if (SA.Size != SB.Size) { return SA.Size > SB.Size; }
		if (SA.Count != SB.Count) { return SA.Count > SB.Count; }
		return FNameLexicalLess()(A, B);
	});

	FSlateFontInfo BigFont = FAppStyle::GetFontStyle("NormalFont");
	BigFont.Size = 24;

	FSlateFontInfo SmallFont = FAppStyle::GetFontStyle("NormalFont");
	SmallFont.Size = 10;

	// --- Build frequency wrap box ---
	TSharedRef<SWrapBox> FrequencyWrap = SNew(SWrapBox).UseAllottedSize(true);
	for (const FName& Freq : SortedFrequencies)
	{
		const FFreqStats& Stats = FrequencyStats.FindChecked(Freq);
		if (Stats.Count <= 0)
		{
			continue;
		}

		// "Pixel: 4,231 (28.1 MB)" or "Compute: 890" if no size data
		FString Label = FString::Printf(TEXT("%s: %s"), *Freq.ToString(), *FText::AsNumber(Stats.Count).ToString());
		if (bHasSize && Stats.Size > 0)
		{
			Label += FString::Printf(TEXT(" (%s)"), *UE::ShaderAudit::Utils::FormatBytes(Stats.Size));
		}

		FrequencyWrap->AddSlot()
		.Padding(0.0f, 0.0f, 16.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Label))
		];
	}

	// --- Build stat columns ---
	auto MakeStatColumn = [&BigFont](const FText& Label, const FText& Value) -> TSharedRef<SVerticalBox>
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(Label)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(Value)
				.Font(BigFont)
			];
	};

	TSharedRef<SHorizontalBox> StatsRow = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 32.0f, 0.0f).VAlign(VAlign_Bottom)
		[
			MakeStatColumn(
				LOCTEXT("UniqueShadersLabel", "Unique Shaders"),
				FText::AsNumber(Session->UniqueShaders.Num()))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 32.0f, 0.0f).VAlign(VAlign_Bottom)
		[
			MakeStatColumn(
				LOCTEXT("TotalMaterialsLabel", "Total Materials"),
				FText::AsNumber(Session->UniqueMaterials.Num()))
		];

	if (bHasSize)
	{
		StatsRow->AddSlot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 32.0f, 0.0f)
		.VAlign(VAlign_Bottom)
		[
			MakeStatColumn(
				LOCTEXT("UniqueSizeLabel", "Unique Size"),
				FText::FromString(UE::ShaderAudit::Utils::FormatBytes(UniqueBytes)))
		];

		if (DiskBytes > UniqueBytes)
		{
			const double Percent = ((double)DiskBytes / UniqueBytes - 1.0) * 100.0;
			FString DiskText = FString::Printf(TEXT("%s (+%.0f%%)"), *UE::ShaderAudit::Utils::FormatBytes(DiskBytes), Percent);

			StatsRow->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Bottom)
			[
				MakeStatColumn(
					LOCTEXT("DiskSizeLabel", "Disk Size (cross-archive duplication)"),
					FText::FromString(DiskText))
			];
		}
	}

	// --- Assemble ---
	TSharedRef<SVerticalBox> CardContent = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			StatsRow
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			FrequencyWrap
		];

	ChildSlot
	[
		SNew(SBorder)
		.Padding(12.0f)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		[
			CardContent
		]
	];
}

#undef LOCTEXT_NAMESPACE
