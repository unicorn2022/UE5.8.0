// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Widgets/SCompoundWidget.h"

namespace UE::MetaHuman
{

/** Result returned by the package destination dialog. */
enum class EPackageDestinationMode : uint8
{
	/** User closed the dialog without making a choice. */
	Cancelled,
	/** Package all selected assets together into one .mhpkg file chosen by the user. */
	CombinedArchive,
	/** Package each selected asset into its own .mhpkg file inside a folder chosen by the user. */
	SeparateArchives,
};

DECLARE_DELEGATE_OneParam(FOnPackageDestinationModeSelected, EPackageDestinationMode);

/**
 * Modal dialog that asks whether the user wants to package selected assets into one combined
 * archive or into separate per-asset archives. Shown only when two or more assets are selected.
 */
class SPackageDestinationDialog final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPackageDestinationDialog)
		{
		}
		/** Number of assets being packaged — used in the dialog title/description. */
		SLATE_ARGUMENT(int32, AssetCount)
		/** Called when the user makes a choice or cancels. */
		SLATE_EVENT(FOnPackageDestinationModeSelected, OnModeSelected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnCombinedClicked();
	FReply OnSeparateClicked();
	FReply OnCancelClicked();

	void CloseParentWindow();

	FOnPackageDestinationModeSelected OnModeSelectedCallback;
};

} // namespace UE::MetaHuman
