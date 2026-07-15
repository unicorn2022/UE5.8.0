// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChooserOutlinerItemDetails.h"

#include "UAFAnimChooserOutlinerData.h"
#include "Styling/StyleColors.h"
#include "ChooserEditorStyle.h"
#include "WorkspaceItemMenuContext.h"
#include "ToolMenus.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "IWorkspaceEditor.h"
#include "ScopedTransaction.h"
#include "Chooser.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/AssetRegistryTagsContext.h"

#define LOCTEXT_NAMESPACE "UAFChooserOutlinerItemDetails"

namespace UE::UAF::ChooserEditor
{
	
	FString FChooserOutlinerItemDetails::GetDisplayString(const FWorkspaceOutlinerItemExport& Export) const
	{			
		if (const FUAFChooserOutlinerItemData* Data = Export.GetData().GetPtr<FUAFChooserOutlinerItemData>())
		{
			if (Data->bIsNestedObject)
			{
				FString ObjectName = Data->ObjectPath.GetSubPathString();
				int32 DotIndex = INDEX_NONE;
				if (ObjectName.FindLastChar('.', DotIndex))
				{
					ObjectName.RightChopInline(DotIndex + 1);
				}
				return ObjectName;
			}
			else
			{
				return Data->ObjectPath.GetAssetName();
			}
		}
		return "";
	}
		
	FSlateColor FChooserOutlinerItemDetails::GetItemColor(const FWorkspaceOutlinerItemExport& Export) const
	{
		if (const FUAFChooserOutlinerItemData* Data = Export.GetData().GetPtr<FUAFChooserOutlinerItemData>())
		{
			if (Data->bIsNestedObject)
			{
				return EStyleColor::AccentPink;
			}
			return EStyleColor::AccentBlue;
		}
		
		return FSlateColor::UseForeground();
	}
	
	const FSlateBrush* FChooserOutlinerItemDetails::GetItemIcon(const FWorkspaceOutlinerItemExport& Export) const
	{
		if (const FUAFChooserOutlinerItemData* Data = Export.GetData().GetPtr<FUAFChooserOutlinerItemData>())
		{
			if (Data->bIsNestedObject)
			{
				return UE::ChooserEditor::FChooserEditorStyle::Get().GetBrush("ChooserEditor.NestedChooserIcon");
			}
			return UE::ChooserEditor::FChooserEditorStyle::Get().GetBrush("ChooserEditor.ChooserTableIconSmall");
		}
		
		return nullptr;
	}

	bool FChooserOutlinerItemDetails::HandleDoubleClick(const FToolMenuContext& ToolMenuContext) const
	{
		const UWorkspaceItemMenuContext* WorkspaceItemContext = ToolMenuContext.FindContext<UWorkspaceItemMenuContext>();
		const UAssetEditorToolkitMenuContext* AssetEditorContext = ToolMenuContext.FindContext<UAssetEditorToolkitMenuContext>(); 
		if (WorkspaceItemContext && AssetEditorContext)
		{				
			if(const TSharedPtr<UE::Workspace::IWorkspaceEditor> WorkspaceEditor = StaticCastSharedPtr<UE::Workspace::IWorkspaceEditor>(AssetEditorContext->Toolkit.Pin()))
			{
				const uint32 NumSelectedExports = WorkspaceItemContext->SelectedExports.Num();				
				if (NumSelectedExports == 1)
				{
					const FWorkspaceOutlinerItemExport& SelectedExport = WorkspaceItemContext->SelectedExports[0].GetResolvedExport();
					
					if (const FUAFChooserOutlinerItemData* Data = SelectedExport.GetData().GetPtr<FUAFChooserOutlinerItemData>())
					{
						if (Data->bExternalAsset)
						{
							if(UObject* Object = Data->ObjectPath.TryLoad())
							{
								GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Object);
							}
						}
						else
						{
							WorkspaceEditor->OpenExports({SelectedExport});
						}
						return true;
					}
				}					
			}			
		}

		return false;
	}

	bool FChooserOutlinerItemDetails::CanDelete(const FWorkspaceOutlinerItemExport& Export) const
	{
		if (const FUAFChooserOutlinerItemData* Data = Export.GetData().GetPtr<FUAFChooserOutlinerItemData>())
		{
			return Data->bIsNestedObject;
		}
	
		return false;
	}
	
	void FChooserOutlinerItemDetails::Delete(TConstArrayView<FWorkspaceOutlinerItemExport> Exports) const
   	{
		TArray<UObject*> ModifiedAssets;
		{
			FScopedTransaction Transaction(LOCTEXT("Delete Nested Chooser(s)", "Delete Nested Chooser(s)"));

			for(const FWorkspaceOutlinerItemExport& Export : Exports)
			{
				if (const FUAFChooserOutlinerItemData* Data = Export.GetData().GetPtr<FUAFChooserOutlinerItemData>())
				{
					if (Data->bIsNestedObject)
					{
						if (UObject* Object = Data->ObjectPath.ResolveObject())
						{
							if (UChooserTable* Chooser = Cast<UChooserTable>(Object))
							{
								UChooserTable* RootChooser = Chooser->GetRootChooser();
								RootChooser->DeleteNestedChooser(Chooser);
								ModifiedAssets.AddUnique(RootChooser);
							}
							else if (UChooserTable* RootChooser = Cast<UChooserTable>(Object->GetOuter()))
							{
								RootChooser->DeleteNestedObject(Object);
								ModifiedAssets.AddUnique(RootChooser);
							}
						}
					}
				}
			}
		}

		if (IAssetRegistry* AssetRegistry = IAssetRegistry::Get())
		{
			for(UObject* Asset : ModifiedAssets)
			{
				AssetRegistry->AssetUpdateTags(Asset, EAssetRegistryTagsCaller::Fast);
			}
		}
   	}
	
	bool FChooserOutlinerItemDetails::CanRename(const FWorkspaceOutlinerItemExport& Export) const
	{
		if (const FUAFChooserOutlinerItemData* Data = Export.GetData().GetPtr<FUAFChooserOutlinerItemData>())
		{
			return Data->bIsNestedObject;
		}
	
		return false;
	}
	
	
	void FChooserOutlinerItemDetails::Rename(const FWorkspaceOutlinerItemExport& Export, const FText& InName) const
	{
		if (const FUAFChooserOutlinerItemData* Data = Export.GetData().GetPtr<FUAFChooserOutlinerItemData>())
		{
			if (Data->bIsNestedObject)
			{
				if (UObject* NestedObject = Cast<UObject>(Data->ObjectPath.ResolveObject()))
				{
					if (NestedObject->GetName() != InName.ToString())
					{
						FScopedTransaction Transaction(LOCTEXT("Rename Nested Object", "Rename Nested Object"));

						NestedObject->Modify();
						NestedObject->Rename(*InName.ToString());
						
						if (IAssetRegistry* AssetRegistry = IAssetRegistry::Get())
						{
							if (UChooserTable* NestedChooser = Cast<UChooserTable>(NestedObject))
							{
								AssetRegistry->AssetUpdateTags(NestedChooser->GetRootChooser(), EAssetRegistryTagsCaller::Fast);
							}
							else
							{
								AssetRegistry->AssetUpdateTags(NestedObject->GetOuter(), EAssetRegistryTagsCaller::Fast);
							}
						}
						
						NestedObject->PostEditChange();
					}

				}
			}
		}
			
	}
	
	bool FChooserOutlinerItemDetails::ValidateName(const FWorkspaceOutlinerItemExport& Export, const FText& InName, FText& OutErrorMessage) const
	{
		if (const FUAFChooserOutlinerItemData* Data = Export.GetData().GetPtr<FUAFChooserOutlinerItemData>())
		{
			if (Data->bIsNestedObject)
			{
				if (UObject* Object = Cast<UObject>(Data->ObjectPath.ResolveObject()))
				{
					FString StringName = InName.ToString();
					if (Object->GetName() == InName.ToString())
					{
						// don't throw an error for same name... just do nothing on enter
						return true;
					}
					else if (UObject* Outer = Object->GetOuter())
					{
						if (UObject* ExistingNestedObject = FindObject<UObject>(Outer, *StringName))
						{
							OutErrorMessage = LOCTEXT("SameNameError", "Another Nested Object by this name already exists");
							return false;
						}
						else
						{
							if (!FName(InName.ToString()).IsValidObjectName(OutErrorMessage))
							{
								return false;
							}
						}
					}
					return true;
				}
			}
		}
		
		return false;
	}
}

#undef LOCTEXT_NAMESPACE