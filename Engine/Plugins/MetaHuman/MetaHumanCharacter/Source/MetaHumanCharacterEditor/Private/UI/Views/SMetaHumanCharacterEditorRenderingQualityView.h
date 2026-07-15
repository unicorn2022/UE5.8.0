// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include "MetaHumanCharacterEditorRenderingQualitySettings.h"

class SMetaHumanCharacterEditorTextComboBox;
class IDetailsView;

/** View for displaying meta human render quality */
class SMetaHumanCharacterEditorRenderingQualityView : public SCompoundWidget, public FGCObject
{
public:
	DECLARE_DELEGATE_OneParam(FOnRenderingQualityProfileUpdate, int32)

	SLATE_BEGIN_ARGS(SMetaHumanCharacterEditorRenderingQualityView) {}

		/** Called when render quality profile is updated. */
		SLATE_EVENT(FOnRenderingQualityProfileUpdate, OnRenderingQualityProfileUpdate)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const int32 InActiveProfileIndex);

	//~ Begin FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;
	//~ End FGCObject interface

	void SetSelectedItem(const int32 InIndex) const;

private:
	void CreateSettingsView();
	void RebuildProfileNames();
	void SwitchToProfile(const int32 InIndex) const;
	void SwitchToProfile(const int32 InIndex, const bool bBroadcastSwitch) const;

	FReply OnAddProfileClicked();
	FReply OnCloneProfileClicked();
	FReply OnRemoveOrResetProfileClicked();
	void CreateAndSelectNewProfile(FMetaHumanCharacterRenderingQualityProfile& InRenderingQuality);

	TSharedPtr<IDetailsView> SettingsView;
	TSharedPtr<SMetaHumanCharacterEditorTextComboBox> ProfileComboBox;
	TArray<TSharedPtr<FString>> ProfileNames;
	TObjectPtr<UMetaHumanCharacterRenderingQualityProfileProxy> Proxy;
	FOnRenderingQualityProfileUpdate OnRenderingQualityProfileUpdate;
};
