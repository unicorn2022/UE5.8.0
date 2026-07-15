// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAFChooserEditorModule.h"

#include <IChooserTableView.h>

#include "Modules/ModuleManager.h"
#include "IAnimNextEditorModule.h"
#include "IWorkspaceEditorModule.h"
#include "IWorkspaceEditor.h"
#include "UAFGraphChooserWidget.h"
#include "AnimNode/UAFAnimNodeWidget.h"
#include "UAFVariableMenus.h"
#include "UAFAnimChooser.h"
#include "Toolkits/AssetEditorModeUILayer.h"
#include "ChooserEditorStyle.h"
#include "ChooserPropertyAccess.h"
#include "ContextObjectStore.h"
#include "EditorModeManager.h"
#include "IChooserEditorModule.h"
#include "UAFChooserEditorMode.h"
#include "AnimNode/UAFChooserPlayerNode.h"

#include "ChooserOutlinerItemDetails.h"
#include "ToolMenus.h"
#include "UAFAnimNodeEditor/Public/UAFAnimNodeDataFactory.h"

#define LOCTEXT_NAMESPACE "UAFChooserEditorModule"

namespace UE::UAF::ChooserEditor
{
	
void MakeParentExport(UObject* Object, FWorkspaceOutlinerItemExport& RootExport, FWorkspaceOutlinerItemExport& Export)
{
	if (RootExport.GetTopLevelAssetPath() == Object)
	{
		Export = RootExport;
	}
	else
	{
		FWorkspaceOutlinerItemExport ParentExport;
		MakeParentExport(Object->GetOuter(), RootExport, ParentExport);
		
		Export = FWorkspaceOutlinerItemExport(FName(Object->GetPathName()), ParentExport);
	}
}

// create a WorkspaceOutlinerItemExport for a chooser or nested chooser for use with FWorkspaceEditor::OpenExports
void MakeChooserExport(UChooserTable* Chooser, FWorkspaceOutlinerItemExport &OutExport)
{
	FWorkspaceOutlinerItemExport RootChooserAssetExport(FWorkspaceOutlinerItemExport(Chooser->GetRootChooser()->GetFName(), Chooser->GetRootChooser()));

	if (Chooser == Chooser->GetRootChooser())
	{
		OutExport = RootChooserAssetExport;
	}
	else
	{
		FWorkspaceOutlinerItemExport ParentExport;
		MakeParentExport(Chooser->GetOuter(), RootChooserAssetExport, ParentExport);

		OutExport = FWorkspaceOutlinerItemExport(FName(Chooser->GetPathName()), ParentExport);
	}
}

void FModule::StartupModule()
{
	RegisterChooserWidgets();
	RegisterChooserAnimNodeWidgets();
	RegisterVariableMenus();
	
	ChooserClassPath = FUAFAnimNodeDataFactory::RegisterAsset<Chooser::FUAFChooserPlayerNodeData, UUAFAnimChooserTable>(
		[](UUAFAnimChooserTable* Chooser)
			{
				Chooser::FUAFChooserPlayerNodeData ChooserPlayer;
				ChooserPlayer.Chooser = Chooser;
				return ChooserPlayer;
			});

	Editor::IAnimNextEditorModule& AnimNextEditorModule = FModuleManager::Get().LoadModuleChecked<Editor::IAnimNextEditorModule>("UAFEditor");	
	AnimNextEditorModule.AddWorkspaceSupportedAssetClass(UUAFAnimChooserTable::StaticClass()->GetClassPathName());
	
	Workspace::IWorkspaceEditorModule& WorkspaceEditorModule = FModuleManager::Get().LoadModuleChecked<Workspace::IWorkspaceEditorModule>("WorkspaceEditor");
	
	Workspace::FObjectDocumentArgs ChooserDocumentArgs(
    		Workspace::FOnMakeDocumentWidget::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext)-> TSharedRef<SWidget>
    		{
    			UObject* Object = InContext.Document.Object;
    			if(const FUAFChooserOutlinerItemData* ItemData = InContext.Document.Export.GetData().GetPtr<FUAFChooserOutlinerItemData>())
    			{
					if(UObject* LoadedObject = ItemData->ObjectPath.TryLoad())
					{
						Object = LoadedObject;
					}
    				
    			}
    				
    			if (UChooserTable* Chooser = Cast<UChooserTable>(Object))
    			{
					TWeakPtr<Workspace::IWorkspaceEditor> WeakWorkspaceEditor = InContext.WorkspaceEditor;
					UE::ChooserEditor::IChooserEditorModule& ChooserEditorModule = FModuleManager::Get().LoadModuleChecked<UE::ChooserEditor::IChooserEditorModule>("ChooserEditor");
					TSharedPtr<UE::ChooserEditor::IChooserTableViewModel> Model = ChooserEditorModule.CreateChooserTableViewModel(Chooser);
    				Model->SetOpenObjectDelegate(UE::ChooserEditor::FOpenObject::CreateLambda([WeakWorkspaceEditor](UObject* Object)
    					{
							if (const TSharedPtr<Workspace::IWorkspaceEditor> SharedWorkspaceEditor = WeakWorkspaceEditor.Pin())
							{
								if (UChooserTable* Chooser = Cast<UChooserTable>(Object))
								{
									FWorkspaceOutlinerItemExport ChooserAssetExport;
									MakeChooserExport(Chooser, ChooserAssetExport);
									SharedWorkspaceEditor->OpenExports({ChooserAssetExport});
								}
							}
    					}
    					));
    				
    				if(!InContext.WorkspaceEditor->GetEditorModeManager().IsModeActive(UUAFChooserEditorMode::EM_UAFChooser))
					{
						InContext.WorkspaceEditor->GetEditorModeManager().ActivateMode(UUAFChooserEditorMode::EM_UAFChooser);
					}
    				
					FEdMode* EditorMode = InContext.WorkspaceEditor->GetEditorModeManager().GetActiveMode(UUAFChooserEditorMode::EM_UAFChooser);

					TSharedPtr<SWidget> ChooserTableView = ChooserEditorModule.CreateChooserTableView(Model, InContext.WorkspaceEditor->GetToolkitCommands());

    				Model->RegisterMenus(InContext.WorkspaceEditor->GetToolkitCommands());
    				
    				TSharedPtr<IDetailsView> DetailsView = InContext.WorkspaceEditor->GetDetailsView();
    				
					Model->SetShowDetailsDelegate(UE::ChooserEditor::FShowDetails::CreateLambda([DetailsViewWeakPtr = DetailsView.ToWeakPtr()](const TArray<UObject*>& Objects)
					{
						if (TSharedPtr<IDetailsView> Details = DetailsViewWeakPtr.Pin())
						{
							Details->SetObjects(Objects, true);
						}
					}));
    				
    				Model->RefreshAll();

    				FToolMenuContext MenuContext;
    				// FAssetEditorToolkit::InitToolMenuContext(MenuContext);
                    UChooserEditorToolMenuContext* Context = NewObject<UChooserEditorToolMenuContext>();
                    Context->ViewModel = Model;
                    MenuContext.AddObject(Context);
                    
					MenuContext.AppendCommandList(InContext.WorkspaceEditor->GetToolkitCommands());

    				return SNew(SVerticalBox)
    				+ SVerticalBox::Slot().AutoHeight()
    				[
						UToolMenus::Get()->GenerateWidget(UE::ChooserEditor::IChooserTableViewModel::ChooserToolbarName, MenuContext)
    				]
    				+ SVerticalBox::Slot()
    				[
    					ChooserTableView.ToSharedRef()
    				];
    			}	
    				
    			return SNullWidget::NullWidget;
    		}
    	), Workspace::WorkspaceTabs::TopMiddleDocumentArea );
    	
    	ChooserDocumentArgs.OnGetTabName = Workspace::FOnGetTabName::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext)
    	{
			if (UChooserTable* Chooser = InContext.Document.GetTypedObject<UChooserTable>())
			{
				return FText::FromName(Chooser->GetFName());
			}
			else
			{
				return LOCTEXT("ChooserTable", "ChooserTable");
			}
    	});
    
    	ChooserDocumentArgs.DocumentEditorMode = UUAFChooserEditorMode::EM_UAFChooser;
    
    	ChooserDocumentArgs.OnGetDocumentBreadcrumbTrail = Workspace::FOnGetDocumentBreadcrumbTrail::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext, TArray<TSharedPtr<Workspace::FWorkspaceBreadcrumb>>& OutBreadcrumbs)
    	{
    		if (UChooserTable* Chooser = InContext.Document.GetTypedObject<UChooserTable>())
    		{
    			while (Chooser)
    			{
    				const TSharedPtr<Workspace::FWorkspaceBreadcrumb>& GraphCrumb = OutBreadcrumbs.Add_GetRef(MakeShared<Workspace::FWorkspaceBreadcrumb>());
					GraphCrumb->OnGetLabel = Workspace::FWorkspaceBreadcrumb::FOnGetBreadcrumbLabel::CreateLambda([ChooserName = Chooser->GetFName()]{ return FText::FromName(ChooserName); });
    
					GraphCrumb->CanSave = Workspace::FWorkspaceBreadcrumb::FCanSaveBreadcrumb::CreateLambda(
						[Chooser]
						{
							return Chooser->GetPackage()->IsDirty();
						}
					);
    
					TWeakPtr<Workspace::IWorkspaceEditor> WeakWorkspaceEditor = InContext.WorkspaceEditor;
					GraphCrumb->OnClicked = Workspace::FWorkspaceBreadcrumb::FOnBreadcrumbClicked::CreateLambda(
						[WeakWorkspaceEditor, Chooser]
						{
							if (const TSharedPtr<Workspace::IWorkspaceEditor> SharedWorkspaceEditor = WeakWorkspaceEditor.Pin())
							{
								FWorkspaceOutlinerItemExport ChooserAssetExport;
								MakeChooserExport(Chooser, ChooserAssetExport);
								SharedWorkspaceEditor->OpenExports({ChooserAssetExport});
							}
						}
					);
					Chooser = Cast<UChooserTable>(Chooser->GetOuter());
    			}
    		}
    	});
    
    	ChooserDocumentArgs.OnGetTabIcon = Workspace::FOnGetTabIcon::CreateLambda([](const Workspace::FWorkspaceEditorContext& InContext)
    	{
			return UE::ChooserEditor::FChooserEditorStyle::Get().GetBrush("ChooserEditor.ChooserTableIconSmall");
    	});
    	
	WorkspaceEditorModule.RegisterObjectDocumentType(UChooserTable::StaticClass()->GetClassPathName(), ChooserDocumentArgs);
	WorkspaceEditorModule.RegisterObjectDocumentType(UUAFAnimChooserTable::StaticClass()->GetClassPathName(), ChooserDocumentArgs);
	
	TSharedPtr<FChooserOutlinerItemDetails> ChooserItemDetails = MakeShareable<FChooserOutlinerItemDetails>(new FChooserOutlinerItemDetails());
	WorkspaceEditorModule.RegisterWorkspaceItemDetails(Workspace::FOutlinerItemDetailsId(FUAFChooserOutlinerItemData::StaticStruct()->GetFName()), StaticCastSharedPtr<UE::Workspace::IWorkspaceOutlinerItemDetails>(ChooserItemDetails));	
}

void FModule::ShutdownModule()
{
	FUAFAnimNodeDataFactory::UnregisterAsset(ChooserClassPath);

	if (UObjectInitialized())
	{
		if(Workspace::IWorkspaceEditorModule* WorkspaceEditorModule = FModuleManager::Get().GetModulePtr<Workspace::IWorkspaceEditorModule>("WorkspaceEditor"))
		{
			WorkspaceEditorModule->UnregisterObjectDocumentType(UChooserTable::StaticClass()->GetClassPathName());
			WorkspaceEditorModule->UnregisterObjectDocumentType(UUAFAnimChooserTable::StaticClass()->GetClassPathName());
			WorkspaceEditorModule->UnregisterWorkspaceItemDetails(Workspace::FOutlinerItemDetailsId(FUAFChooserOutlinerItemData::StaticStruct()->GetFName()));
		}
	
		if( Editor::IAnimNextEditorModule* AnimNextEditorModule = FModuleManager::Get().GetModulePtr<Editor::IAnimNextEditorModule>("UAFEditor"))
		{
			AnimNextEditorModule->RemoveWorkspaceSupportedAssetClass(UUAFAnimChooserTable::StaticClass()->GetClassPathName());
		}
	}
}
	
}

IMPLEMENT_MODULE(UE::UAF::ChooserEditor::FModule, UAFChooserEditor);

#undef LOCTEXT_NAMESPACE
