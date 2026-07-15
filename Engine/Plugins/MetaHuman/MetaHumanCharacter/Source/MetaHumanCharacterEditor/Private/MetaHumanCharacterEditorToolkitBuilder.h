// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ToolkitBuilder.h"

struct FMetaHumanCharacterEditorToolkitSections : FToolkitSections
{
	TSharedPtr<SWidget> ToolCustomWarningsArea = nullptr;
	TSharedPtr<SWidget> ToolViewArea = nullptr;
};

/** A customized Toolkit Builder used for the MetaHuman Character editor implementation */
class FMetaHumanCharacterEditorToolkitBuilder : public FToolkitBuilder
{
public:
	/**
	 * Constructor
	 *
	 * @param ToolbarCustomizationName the name of the  customization fr the category toolbar
	 * @param InToolkitCommandList  the toolkit FUICommandList
	 * @param InToolkitSections The FToolkitSections for this toolkit builder
	 */
	explicit FMetaHumanCharacterEditorToolkitBuilder(
		FName ToolbarCustomizationName,
		TSharedPtr<FUICommandList> InToolkitCommandList,
		TSharedPtr<FToolkitSections> InToolkitSections);

	/** default constructor */
	explicit FMetaHumanCharacterEditorToolkitBuilder(FToolkitBuilderArgs& Args);

	/**
	 * Adds a palette and tracks its display order locally for the custom category column.
	 * Required because base FToolkitBuilder::LoadCommandArray is private; we need the
	 * ordered list to build the wrapping-label category buttons.
	 * Use this instead of FToolkitBuilder::AddPalette when constructing the MetaHuman editor.
	 */
	void AddPaletteCustom(TSharedPtr<FToolPalette> Palette);
	void AddPaletteCustom(TSharedPtr<FEditablePalette> Palette);

protected:
	//~Begin FToolElementRegistrationArgs interface
	virtual TSharedPtr<SWidget> GenerateWidget() override;
	//~End FToolElementRegistrationArgs interface

	/** Builds the custom left-hand category column with wrapping text labels. */
	TSharedRef<SWidget> BuildCategoryColumnWidget();

private:
	/** Reference to the custom Tool Warnings section */
	TSharedPtr<SWidget> ToolCustomWarningsArea;

	/** Reference to the Tool View area section */
	TSharedPtr<SWidget> ToolViewArea;

	/** Ordered list of category command names, in the order palettes were added. */
	TArray<FName> OrderedLoadCommandNames;

	/**
	 * Cached root SSplitter wrapping the inherited MainContentVerticalBox.
	 * Built once on the first GenerateWidget() call and reused on subsequent calls so that
	 * MainContentVerticalBox is never re-parented into a freshly-created SSplitter (which
	 * would trigger Slate's single-parent assertion).
	 */
	TSharedPtr<SWidget> CachedRootWidget;
};
