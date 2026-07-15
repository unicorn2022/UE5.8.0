// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Framework/SlateDelegates.h"
#include "Widgets/SCompoundWidget.h"

struct FMetaHumanAssetDescription;

namespace UE::MetaHuman
{

class SAssetGroupItemDetails;

DECLARE_DELEGATE(FOnVerify);
DECLARE_DELEGATE(FOnPackage);

/** Widget to display details of an AssetGroup: name, icon, contents, verification report etc. */
class SAssetGroupItemView final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssetGroupItemView)
		{
		}
		SLATE_EVENT(FOnVerify, OnVerify)
		SLATE_EVENT(FOnPackage, OnPackage)
		SLATE_ATTRIBUTE(bool, EnablePackageButton)
		/** Text to use on the Verify button — allows the caller to show e.g. "Verify 5 Items". */
		SLATE_ATTRIBUTE(FText, VerifyButtonText)
		/** Text to use on the Package button — allows the caller to show e.g. "Package 5 Items...". */
		SLATE_ATTRIBUTE(FText, PackageButtonText)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Set the item whose details are shown in the right pane.
	 * @param AssetDescription  The item to display (may be null to clear).
	 * @param OtherSelectedCount  Number of additional items selected alongside this one (0 = only item).
	 */
	void SetItem(TSharedPtr<FMetaHumanAssetDescription> AssetDescription, int32 OtherSelectedCount = 0);

private:
	FReply OnVerify() const;
	FReply OnPackage() const;

	bool EnableVerifyButton() const;

	// Data
	TSharedPtr<SAssetGroupItemDetails> ItemDetails;

	// Callbacks
	FOnVerify OnVerifyCallback;
	FOnPackage OnPackageCallback;
};

} // namespace UE::MetaHuman
