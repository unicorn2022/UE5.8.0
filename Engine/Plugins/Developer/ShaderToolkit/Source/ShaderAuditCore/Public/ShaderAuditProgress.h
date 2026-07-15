// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopedSlowTask.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/Application/SlateApplication.h"

/**
 * RAII progress reporter that works in both editor and standalone.
 * Editor: FScopedSlowTask (modal dialog with cancel).
 * Standalone: modal SWindow with SProgressBar.
 */
class FScopedProgressTask
{
public:
	FScopedProgressTask(float InTotalWork, const FText& InDescription, bool bInSilent = false)
		: TotalWork(FMath::Max(InTotalWork, 1.f))
		, CompletedWork(0.f)
		, Description(InDescription)
		, bCancelRequested(false)
		, bSilent(bInSilent)
	{
		if (bSilent)
		{
			return;
		}

		if (GIsEditor)
		{
			SlowTask = MakeUnique<FScopedSlowTask>(TotalWork, InDescription);
			SlowTask->MakeDialog(true);
		}
		else if (FSlateApplication::IsInitialized())
		{
			ProgressWindow = SNew(SWindow)
				.Title(InDescription)
				.ClientSize(FVector2D(400, 110))
				.IsTopmostWindow(true)
				.SupportsMinimize(false)
				.SupportsMaximize(false)
				.SizingRule(ESizingRule::FixedSize)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(12.f, 12.f, 12.f, 4.f)
					[
						SAssignNew(StatusText, STextBlock)
						.Text(InDescription)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(12.f, 4.f, 12.f, 4.f)
					[
						SAssignNew(ProgressBar, SProgressBar)
						.Percent(0.f)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Right)
					.Padding(12.f, 4.f, 12.f, 8.f)
					[
						static_cast<TSharedRef<SWidget>>(
							SNew(SButton)
							.Text(NSLOCTEXT("ShaderAuditProgress", "Cancel", "Cancel"))
							.OnClicked_Lambda([this]()
							{
								bCancelRequested = true;
								return FReply::Handled();
							}))
					]
				];

			FSlateApplication::Get().AddWindow(ProgressWindow.ToSharedRef());
		}
	}

	~FScopedProgressTask()
	{
		if (ProgressWindow.IsValid())
		{
			ProgressWindow->RequestDestroyWindow();
			ProgressWindow.Reset();
		}
	}

	void EnterProgressFrame(float Work = 1.f, const FText& Text = FText())
	{
		CompletedWork += Work;

		if (SlowTask)
		{
			SlowTask->EnterProgressFrame(Work, Text);
		}
		else if (ProgressWindow.IsValid())
		{
			float Fraction = FMath::Clamp(CompletedWork / TotalWork, 0.f, 1.f);
			ProgressBar->SetPercent(Fraction);
			if (!Text.IsEmpty())
			{
				StatusText->SetText(Text);
			}
			FSlateApplication::Get().Tick();
		}
	}

	bool ShouldCancel() const
	{
		if (SlowTask)
		{
			return SlowTask->ShouldCancel();
		}
		return bCancelRequested;
	}

private:
	float TotalWork;
	float CompletedWork;
	FText Description;
	bool bCancelRequested;
	bool bSilent;
	TUniquePtr<FScopedSlowTask> SlowTask;
	TSharedPtr<SWindow> ProgressWindow;
	TSharedPtr<SProgressBar> ProgressBar;
	TSharedPtr<STextBlock> StatusText;
};
