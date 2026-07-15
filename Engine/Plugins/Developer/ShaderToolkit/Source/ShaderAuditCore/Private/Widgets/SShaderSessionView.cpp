// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SShaderSessionView.h"

#include "IShaderAuditExtension.h"
#include "ShaderAuditSession.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SInspectShaderSessionWidget.h"
#include "Widgets/SShaderCostTreeMap.h"
#include "Widgets/SSHKEntryListWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ShaderSessionView"

// ============================================================================
// SShaderSessionView
// ============================================================================

void SShaderSessionView::Construct(const FArguments& InArgs)
{
	Session = InArgs._Session;
	OnFetchMaterialHierarchyHook = InArgs._OnFetchMaterialHierarchy;
	ChildSlot
	[
		SNew(SVerticalBox)

		// Toolbar: view toggle
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.f)
		[
			SAssignNew(ToolbarBox, SHorizontalBox)

			// View toggle buttons
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("SpreadsheetView", "Spreadsheets"))
				.OnClicked_Lambda([this]()
				{
					if (ViewSwitcher.IsValid()) { ViewSwitcher->SetActiveWidgetIndex(0); }
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("RawSHKView", "Raw SHK"))
				.OnClicked_Lambda([this]()
				{
					if (ViewSwitcher.IsValid()) { ViewSwitcher->SetActiveWidgetIndex(1); }
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("TreemapView", "Cost Treemap"))
				.OnClicked_Lambda([this]()
				{
					if (ViewSwitcher.IsValid()) { ViewSwitcher->SetActiveWidgetIndex(2); }
					return FReply::Handled();
				})
			]
			// "Fetch Material Hierarchy" button -- visible only when the caller bound a hook
			// (typically: editor module that walks the Asset Registry).
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(16.f, 0.f, 2.f, 0.f)
			[
				SNew(SButton)
				.Visibility_Lambda([this]()
				{
					return OnFetchMaterialHierarchyHook.IsBound() ? EVisibility::Visible : EVisibility::Collapsed;
				})
				.Text_Lambda([this]()
				{
					return Session.IsValid() && Session->HasMaterialHierarchy()
						? LOCTEXT("HierarchyLoaded", "Material Hierarchy Loaded")
						: LOCTEXT("FetchHierarchy", "Fetch Material Hierarchy");
				})
				.ToolTipText(LOCTEXT("FetchHierarchyTip", "Fetch material parent hierarchy from the editor's Asset Registry."))
				.OnClicked_Lambda([this]()
				{
					if (!Session.IsValid())
					{
						return FReply::Handled();
					}

					OnFetchMaterialHierarchyHook.ExecuteIfBound(Session);

					if (Session->HasMaterialHierarchy() && CostTreeMapWidget.IsValid())
					{
						CostTreeMapWidget->RebuildFromFilters();
					}

					return FReply::Handled();
				})
			]
		]

		// Content area
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SAssignNew(ViewSwitcher, SWidgetSwitcher)
			.WidgetIndex(0)

			// Index 0: Spreadsheet view
			+ SWidgetSwitcher::Slot()
			[
				SAssignNew(InspectWidget, SInspectShaderSessionWidget)
			]

			// Index 1: Raw SHK entry list
			+ SWidgetSwitcher::Slot()
			[
				SAssignNew(RawSHKWidget, SSHKEntryListWidget)
			]

			// Index 2: Cost Treemap view
			+ SWidgetSwitcher::Slot()
			[
				SAssignNew(CostTreeMapWidget, SShaderCostTreeMap)
				.OnExtendAssetContextMenu(InArgs._OnExtendAssetContextMenu)
				.OnOpenAssetInContentBrowser(InArgs._OnOpenAssetInContentBrowser)
			]
		]
	];

	// Initialize with session data
	if (Session.IsValid())
	{
		if (InspectWidget.IsValid())
		{
			InspectWidget->SetSession(Session);
		}
		if (CostTreeMapWidget.IsValid())
		{
			CostTreeMapWidget->SetSession(Session);
		}
		if (RawSHKWidget.IsValid())
		{
			RawSHKWidget->SetSession(Session);
		}
	}

	// Discover and integrate registered extensions
	TArray<IShaderAuditExtension*> Extensions = IModularFeatures::Get().GetModularFeatureImplementations<IShaderAuditExtension>(IShaderAuditExtension::FeatureName);
	for (IShaderAuditExtension* Extension : Extensions)
	{
		AddExtensionContributions(*Extension);
	}

	// Listen for late-registering / unregistering extensions
	IModularFeatures::Get().OnModularFeatureRegistered().AddRaw(this, &SShaderSessionView::OnModularFeatureRegistered);
	IModularFeatures::Get().OnModularFeatureUnregistered().AddRaw(this, &SShaderSessionView::OnModularFeatureUnregistered);
}

SShaderSessionView::~SShaderSessionView()
{
	IModularFeatures::Get().OnModularFeatureRegistered().RemoveAll(this);
	IModularFeatures::Get().OnModularFeatureUnregistered().RemoveAll(this);
}

void SShaderSessionView::AddExtensionContributions(IShaderAuditExtension& Extension)
{
	if (!ToolbarBox.IsValid() || !ViewSwitcher.IsValid())
	{
		return;
	}

	TArray<FShaderAuditExtensionContribution> Contributions = Extension.GetContributions();
	for (const FShaderAuditExtensionContribution& Contribution : Contributions)
	{
		// Add a view tab if the extension provides one
		if (!Contribution.ViewTabLabel.IsEmpty() && Contribution.CreateViewWidget)
		{
			const int32 ViewIndex = NextExtensionViewIndex++;
			TSharedRef<SWidget> ViewWidget = Contribution.CreateViewWidget(Session);
			ViewSwitcher->AddSlot()[ViewWidget];

			FText Label = Contribution.ViewTabLabel;
			ToolbarBox->AddSlot()
				.AutoWidth()
				.Padding(2.f)
				[
					SNew(SButton)
					.Text(Label)
					.OnClicked_Lambda([this, ViewIndex]()
					{
						if (ViewSwitcher.IsValid()) { ViewSwitcher->SetActiveWidgetIndex(ViewIndex); }
						return FReply::Handled();
					})
				];
		}

		// Add toolbar widgets if the extension provides them
		if (Contribution.CreateToolbarWidget)
		{
			TSharedRef<SWidget> ToolbarWidget = Contribution.CreateToolbarWidget(Session);
			ToolbarBox->AddSlot()
				.AutoWidth()
				.Padding(2.f)
				[
					ToolbarWidget
				];
		}
	}
}

void SShaderSessionView::OnModularFeatureRegistered(const FName& Type, IModularFeature* Feature)
{
	if (Type == IShaderAuditExtension::FeatureName)
	{
		AddExtensionContributions(*static_cast<IShaderAuditExtension*>(Feature));
	}
}

void SShaderSessionView::OnModularFeatureUnregistered(const FName& Type, IModularFeature* Feature)
{
	// Extensions are expected to live for the duration of the module.
	// If dynamic removal becomes needed, we can rebuild the toolbar here.
}
#undef LOCTEXT_NAMESPACE
