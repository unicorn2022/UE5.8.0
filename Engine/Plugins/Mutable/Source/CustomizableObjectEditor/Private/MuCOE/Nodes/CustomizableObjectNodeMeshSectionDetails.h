// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CustomizableObjectNodeDetails.h"
#include "MuCOE/SMutableTagListWidget.h"
#include "MuCOE/SMutableSearchComboBox.h"
#include "IDetailCustomization.h"
#include "Widgets/Views/SListView.h"

namespace ESelectInfo { enum Type : int; }

class FString;
class IDetailLayoutBuilder;
class IPropertyHandle;
class UCustomizableObjectNode;
class UCustomizableObjectNodeObject;

/** */
class FCustomizableObjectNodeMeshSectionDetails : public FCustomizableObjectNodeDetails
{
public:
	// Makes a new instance of this detail layout class for a specific detail view requesting it 
	static TSharedRef<IDetailCustomization> MakeInstance();

	// ILayoutDetails interface
	void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;

protected:

	/** */
	virtual void OnEnableTagsPropertyChanged();

private:

	/** Disables/Enables the tags property widget. */
	bool IsTagsPropertyWidgetEnabled() const;

	/** Set the widget tooltip. */
	FText TagsPropertyWidgetTooltip() const;

private:

	class UCONodeSkeletalMeshSection* Node = nullptr;

	/** */
	TSharedPtr<IPropertyHandle> TagsPropertyHandle;

	/** */
	TSharedPtr<SMutableTagListWidget> TagListWidget;

};
