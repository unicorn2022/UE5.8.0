// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Features/Browser/ViewModels/List/ISandboxColumnBehavior.h"
#include "Features/Browser/ViewModels/List/ISandboxColumnWidgetFactory.h"

namespace UE::SandboxedEditing
{
/** Displays the engine version this sandbox was last used in. */
class FVersionSandboxColumn : public ISandboxColumnBehavior, public ISandboxColumnWidgetFactory
{
public:
	
	//~ Begin ISandboxColumnBehavior Interface
	virtual void PopulateSearchTerms(const TSharedPtr<FSandboxListItem>& InRowData, TArray<FString>& OutSearchTerms) const override;
	//~ End ISandboxColumnBehavior Interface
	
	//~ Begin ISandboxWidgetFactory Interface
	virtual SHeaderRow::FColumn::FArguments MakeColumnArguments() override;
	virtual TSharedRef<SWidget> MakeColumnWidget(const FMakeSandboxColumnWidgetArgs& InArgs) override;
	//~ End ISandboxWidgetFactory Interface
};
}

