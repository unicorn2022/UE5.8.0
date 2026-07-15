// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigAssetActions.h"
#include "ControlRigAssetFactory.h"
#include "ControlRigRuntimeAsset.h"
#include "ControlRig.h"
#include "Editor/RigVMEditorStyle.h"
#include "IControlRigEditorModule.h"

#include "Styling/SlateIconFinder.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "Styling/AppStyle.h" 
#include "Subsystems/AssetEditorSubsystem.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ToolMenus.h"
#include "ContentBrowserMenuContexts.h"
#include "ControlRigBlueprintLegacy.h"
#include "ControlRigEditorAsset.h"
#include "ControlRigEditorModule.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "EditorDirectories.h"
#include "ILevelSequenceEditorToolkit.h"
#include "ControlRigObjectBinding.h"
#include "EditMode/ControlRigEditMode.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "MovieSceneToolsProjectSettings.h"
#include "SBlueprintDiff.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Sequencer/ControlRigParameterTrackEditor.h"
#include "Rigs/RigHierarchyController.h"
#include "ModularRig.h"
#include "UObject/GarbageCollectionSchema.h"

#define LOCTEXT_NAMESPACE "ControlRigAssetActions"

FDelegateHandle FControlRigAssetActions::OnSpawnedSkeletalMeshActorChangedHandle;

void FControlRigAssetActions::OpenAssetEditor( const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor )
{
	if (IsEngineExitRequested())
	{
		return;
	}
	
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		if (UControlRigRuntimeAsset* ControlRigAsset = Cast<UControlRigRuntimeAsset>(*ObjIt))
		{
			if (UControlRigEditorAsset* EditorAsset = Cast<UControlRigEditorAsset>(ControlRigAsset->GetEditorAsset()))
			{
				const bool bBringToFrontIfOpen = true;
				UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
				if (IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(EditorAsset, bBringToFrontIfOpen))
				{
					EditorInstance->FocusWindow(EditorAsset);
				}
				else
				{
					// If any other editors are opened (for example, a BlueprintDiff window), close them 
					AssetEditorSubsystem->CloseAllEditorsForAsset(EditorAsset);
				
					IControlRigEditorModule& ControlRigEditorModule = FModuleManager::LoadModuleChecked<IControlRigEditorModule>("ControlRigEditor");
					ControlRigEditorModule.CreateControlRigEditor(Mode, EditWithinLevelEditor, ControlRigAsset);
				}
			}
		}
	}
}

TSharedPtr<SWidget> FControlRigAssetActions::GetThumbnailOverlay(const FAssetData& AssetData) const
{
	const FSlateBrush* Icon = FSlateIconFinder::FindIconBrushForClass(UControlRigRuntimeAsset::StaticClass());

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetNoBrush())
		.Visibility(EVisibility::HitTestInvisible)
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 3.0f))
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		[
			SNew(SImage)
			.Image(Icon)
		];
}

void FControlRigAssetActions::PerformAssetDiff(UObject* OldAsset, UObject* NewAsset, const FRevisionInfo& OldRevision, const FRevisionInfo& NewRevision) const
{
	UBlueprint* OldBlueprint = Cast<UBlueprint>(OldAsset);
	UBlueprint* NewBlueprint = Cast<UBlueprint>(NewAsset);

	static const FText DiffWindowMessage = LOCTEXT("ControlRigDiffWindow", "Opening a diff window will close the control rig editor. {0}.\nAre you sure?");
	
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (OldAsset)
	{
		for (IAssetEditorInstance* Editor : AssetEditorSubsystem->FindEditorsForAsset(OldAsset))
		{
			const EAppReturnType::Type Answer = FMessageDialog::Open( EAppMsgType::YesNo,
					FText::Format(DiffWindowMessage, FText::FromString(OldBlueprint->GetName())));
			if(Answer == EAppReturnType::No)
			{
			   return;
			}
		}
	}
	if (NewAsset)
	{
		for (IAssetEditorInstance* Editor : AssetEditorSubsystem->FindEditorsForAsset(NewAsset))
		{
			const EAppReturnType::Type Answer = FMessageDialog::Open( EAppMsgType::YesNo,
					FText::Format(DiffWindowMessage, FText::FromString(NewBlueprint->GetName())));
			if(Answer == EAppReturnType::No)
			{
				return;
			}
		}
	}

	if (OldAsset)
	{
		AssetEditorSubsystem->CloseAllEditorsForAsset(OldAsset);
	}
	
	if (NewAsset)
	{
		AssetEditorSubsystem->CloseAllEditorsForAsset(NewAsset);
	}

#if WITH_RIGVMLEGACYEDITOR
	SBlueprintDiff::CreateDiffWindow(OldBlueprint, NewBlueprint, OldRevision, NewRevision, GetSupportedClass());
#endif
}

TWeakPtr<IClassTypeActions> FControlRigAssetActions::GetClassTypeActions(const FAssetData& AssetData) const
{
	FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
	return AssetToolsModule.Get().GetClassTypeActionsForClass(UControlRigEditorAsset::StaticClass());
}

void FControlRigAssetActions::ExtendSketalMeshToolMenu()
{
	TArray<UToolMenu*> MenusToExtend;
	MenusToExtend.Add(UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.SkeletalMesh.CreateSkeletalMeshSubmenu"));
	MenusToExtend.Add(UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.Skeleton.CreateSkeletalMeshSubmenu"));

	for(UToolMenu* Menu : MenusToExtend)
	{
		if (Menu == nullptr)
		{
			continue;
		}
		
		FToolMenuSection* Section = Menu->FindSection("ControlRig");
		if (!Section)
		{
			Section = &Menu->AddSection("ControlRig", LOCTEXT("ControlRigSectionName", "Control Rig"));
		}
		Section->AddDynamicEntry("CreateControlRigAsset", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			if (!CVarRigVMBlueprintIndependentAssets->GetBool())
			{
				return;
			}
			
			UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
			if (Context)
			{
				if (Context->SelectedAssets.Num() > 0)
				{
					static constexpr bool bModularRig = true;
					InSection.AddMenuEntry(
						"CreateControlRigAsset",
						LOCTEXT("CreateControlRig", "Control Rig"),
						LOCTEXT("CreateControlRig_ToolTip", "Creates a control rig and preconfigures it for this asset"),
						FSlateIcon(FRigVMEditorStyle::Get().GetStyleSetName(), "RigVM", "RigVM.Unit"),
						FExecuteAction::CreateLambda([InSelectedAssets = Context->SelectedAssets]()
						{
							TArray<UObject*> SelectedObjects;
							SelectedObjects.Reserve(InSelectedAssets.Num());

							for (const FAssetData& Asset : InSelectedAssets)
							{
								if (UObject* LoadedAsset = Asset.GetAsset())
								{
									SelectedObjects.Add(LoadedAsset);
								}
							}


							for (UObject* SelectedObject : SelectedObjects)
							{
								CreateControlRigFromSkeletalMeshOrSkeleton(SelectedObject, !bModularRig);
							}
						})
					);
					InSection.AddMenuEntry(
						"CreateModularRigAsset",
						LOCTEXT("CreateModularRig", "Modular Rig"),
						LOCTEXT("CreateModularRig_ToolTip", "Creates a modular rig and preconfigures it for this asset"),
						FSlateIcon(FRigVMEditorStyle::Get().GetStyleSetName(), "RigVM", "RigVM.Unit"),
						FExecuteAction::CreateLambda([InSelectedAssets = Context->SelectedAssets]()
						{
							TArray<UObject*> SelectedObjects;
							SelectedObjects.Reserve(InSelectedAssets.Num());

							for (const FAssetData& Asset : InSelectedAssets)
							{
								if (UObject* LoadedAsset = Asset.GetAsset())
								{
									SelectedObjects.Add(LoadedAsset);
								}
							}

							for (UObject* SelectedObject : SelectedObjects)
							{
								CreateControlRigFromSkeletalMeshOrSkeleton(SelectedObject, bModularRig);
							}
						})
					);
				}
			}
		}));
	}
}

UControlRigRuntimeAsset* FControlRigAssetActions::CreateNewControlRigAsset(const FString& InDesiredPackagePath, const bool bModularRig)
{
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	UControlRigAssetFactory* Factory = NewObject<UControlRigAssetFactory>();
	Factory->ParentClass = bModularRig ? UModularRig::StaticClass() : UControlRig::StaticClass();

	FString UniquePackageName;
	FString UniqueAssetName;
	AssetToolsModule.Get().CreateUniqueAssetName(InDesiredPackagePath, TEXT(""), UniquePackageName, UniqueAssetName);

	if (UniquePackageName.EndsWith(UniqueAssetName))
	{
		UniquePackageName = UniquePackageName.LeftChop(UniqueAssetName.Len() + 1);
	}

	UObject* NewAsset = AssetToolsModule.Get().CreateAsset(*UniqueAssetName, *UniquePackageName, nullptr, Factory);
	return Cast<UControlRigRuntimeAsset>(NewAsset);
}

UControlRigRuntimeAsset* FControlRigAssetActions::CreateNewControlRigAsset(UControlRigBlueprint* InBlueprint)
{
	FString AssetPath;
	FString BlueprintName;
	FString Extension;
	FPaths::Split(InBlueprint->GetPackage()->GetPathName(), AssetPath, BlueprintName, Extension);
	FString DesiredAssetPath = FString::Printf(TEXT("%s/%s_Asset"), *AssetPath, *BlueprintName);

	UControlRigRuntimeAsset* NewAsset =  CreateNewControlRigAsset(DesiredAssetPath, InBlueprint->IsModularRig());
	UE_LOGF(LogRigVM, Display, "Created asset %ls", *NewAsset->GetPackage()->GetPathName());

	UControlRigBlueprint::CopyBlueprintToAsset(InBlueprint, NewAsset);
	
	return NewAsset;
}


UControlRigRuntimeAsset* FControlRigAssetActions::CreateControlRigFromSkeletalMeshOrSkeleton(UObject* InSelectedObject, const bool bModularRig)
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(InSelectedObject);
	USkeleton* Skeleton = Cast<USkeleton>(InSelectedObject);
	const FReferenceSkeleton* RefSkeleton = nullptr;

	if(SkeletalMesh)
	{
		Skeleton = SkeletalMesh->GetSkeleton();
		RefSkeleton = &SkeletalMesh->GetRefSkeleton();
	}
	else if (Skeleton)
	{
		RefSkeleton = &Skeleton->GetReferenceSkeleton();
	}
	else
	{
		UE_LOGF(LogControlRigEditor, Error, "CreateControlRigFromSkeletalMeshOrSkeleton: Provided object has to be a SkeletalMesh or Skeleton.");
		return nullptr;
	}

	check(RefSkeleton);

	FString PackagePath = InSelectedObject->GetPathName();

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	const TOptional<FString> DefaultControlRigName = AssetToolsModule.Get().GetDefaultAssetNameForClass(UControlRigRuntimeAsset::StaticClass(), nullptr, InSelectedObject);

	FString ControlRigName = DefaultControlRigName.IsSet() ? DefaultControlRigName.GetValue() : FString::Printf(TEXT("%s_CtrlRig"), *InSelectedObject->GetName());

	int32 LastSlashPos = INDEX_NONE;
	if (PackagePath.FindLastChar('/', LastSlashPos))
	{
		PackagePath = PackagePath.Left(LastSlashPos);
	}

	UControlRigRuntimeAsset* NewControlRigRuntimeAsset = CreateNewControlRigAsset(PackagePath / ControlRigName, bModularRig);
	if (NewControlRigRuntimeAsset == nullptr)
	{
		return nullptr;
	}

	UControlRigEditorAsset* NewControlRigEditorAsset = Cast<UControlRigEditorAsset>(NewControlRigRuntimeAsset->GetEditorOnlyData());
	if (NewControlRigEditorAsset == nullptr)
	{
		return nullptr;
	}
	FControlRigEditorModule::Get().CreateRootGraphIfRequired(NewControlRigEditorAsset);

	if(URigHierarchyController* Controller = Cast<IControlRigEditorAssetInterface>(NewControlRigEditorAsset)->GetHierarchyController())
	{
		Controller->ImportBones(*RefSkeleton, NAME_None, false, false, false, false);
		if(SkeletalMesh)
		{
			Controller->ImportCurvesFromSkeletalMesh(SkeletalMesh, NAME_None, false, false);
		}
		else
		{
			Controller->ImportCurves(Skeleton, NAME_None, false, false);
		}
	}
	NewControlRigRuntimeAsset->SourceHierarchyImport = Skeleton;
	NewControlRigRuntimeAsset->SourceCurveImport = Skeleton;
	NewControlRigEditorAsset->PropagateHierarchyFromBPToInstances();

	if(SkeletalMesh)
	{
		NewControlRigEditorAsset->SetPreviewMesh(SkeletalMesh);
	}

	if(!bModularRig)
	{
		NewControlRigEditorAsset->RecompileVM();
	}

	return NewControlRigRuntimeAsset;
}

USkeletalMesh* FControlRigAssetActions::GetSkeletalMeshFromControlRigAsset(const FAssetData& InAsset)
{
	// The asset could be a UControlRigBlueprint, or a UControlRigBlueprintGeneratedClass (in the case of a UEFN rig)
	
	if (InAsset.GetClass() != UControlRigRuntimeAsset::StaticClass())
	{
		return nullptr;
	}
	
	if (const UControlRigRuntimeAsset* RuntimeAsset = Cast<UControlRigRuntimeAsset>(InAsset.GetAsset()))
	{
		if (const UControlRigEditorAsset* EditorAsset = Cast<UControlRigEditorAsset>(RuntimeAsset->GetEditorOnlyData()))
		{
			return EditorAsset->GetPreviewMesh();
		}
	}
	return nullptr;
}

void FControlRigAssetActions::PostSpawningSkeletalMeshActor(AActor* InSpawnedActor, UObject* InAsset)
{
	if (InSpawnedActor->HasAnyFlags(RF_Transient) || InSpawnedActor->bIsEditorPreviewActor)
	{
		return;
	}
	
	OnSpawnedSkeletalMeshActorChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddStatic(&FControlRigAssetActions::OnSpawnedSkeletalMeshActorChanged, InAsset);
}

void FControlRigAssetActions::OnSpawnedSkeletalMeshActorChanged(UObject* InObject, FPropertyChangedEvent& InEvent, UObject* InAsset)
{
	if (!OnSpawnedSkeletalMeshActorChangedHandle.IsValid())
	{
		return;
	}

	// we are waiting for the top level property change event
	// after the spawn.
	if (InEvent.Property != nullptr)
	{
		return;
	}

	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(OnSpawnedSkeletalMeshActorChangedHandle);
	OnSpawnedSkeletalMeshActorChangedHandle.Reset();

	// Create a level sequence but delay until next tick so that the creation of the asset is not in the existing transaction
	GEditor->GetTimerManager()->SetTimerForNextTick([InObject, InAsset]()
	{
		ASkeletalMeshActor* MeshActor = Cast<ASkeletalMeshActor>(InObject);
		check(MeshActor);
		FControlRigAssetStrongReference ControlRigClass(Cast<UControlRigRuntimeAsset>(InAsset));

		if (!ControlRigClass.IsValid())
		{
			return;
		}

		TGuardValue<bool> DisableTrackCreation(FControlRigParameterTrackEditor::bAutoGenerateControlRigTrack, false);

		// find a level sequence in the world, if can't find that, create one
		ULevelSequence* Sequence = ULevelSequenceEditorBlueprintLibrary::GetFocusedLevelSequence();
		if (Sequence == nullptr)
		{
			FString SequenceName = FString::Printf(TEXT("%s_Take1"), *InAsset->GetName());

			FString PackagePath;
			const FString DefaultDirectory = FEditorDirectories::Get().GetLastDirectory(ELastDirectory::NEW_ASSET);
			FPackageName::TryConvertFilenameToLongPackageName(DefaultDirectory, PackagePath);
			if (PackagePath.IsEmpty())
			{
				PackagePath = TEXT("/Game");
			}

			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
			FString UniquePackageName;
			FString UniqueAssetName;
			AssetToolsModule.Get().CreateUniqueAssetName(PackagePath / SequenceName, TEXT(""), UniquePackageName, UniqueAssetName);

			UPackage* Package = CreatePackage(*UniquePackageName);
			Sequence = NewObject<ULevelSequence>(Package, *UniqueAssetName, RF_Public | RF_Standalone);
			Sequence->Initialize(); //creates movie scene
			Sequence->MarkPackageDirty();

			// Notify the asset registry
			FAssetRegistryModule::AssetCreated(Sequence);

			// Set up some sensible defaults
			const UMovieSceneToolsProjectSettings* ProjectSettings = GetDefault<UMovieSceneToolsProjectSettings>();
			FFrameRate TickResolution = Sequence->GetMovieScene()->GetTickResolution();
			Sequence->GetMovieScene()->SetPlaybackRange((ProjectSettings->DefaultStartTime * TickResolution).FloorToFrame(), (ProjectSettings->DefaultDuration * TickResolution).FloorToFrame().Value);

			if (UActorFactory* ActorFactory = GEditor->FindActorFactoryForActorClass(ALevelSequenceActor::StaticClass()))
			{
				if (ALevelSequenceActor* LevelSequenceActor = Cast<ALevelSequenceActor>(GEditor->UseActorFactory(ActorFactory, FAssetData(Sequence), &FTransform::Identity)))
				{
					LevelSequenceActor->SetSequence(Sequence);
				}
			}
		}

		if (Sequence == nullptr)
		{
			return;
		}

		UMovieScene* MovieScene = Sequence->GetMovieScene();

		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Sequence);

		IAssetEditorInstance* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Sequence, false);
		ILevelSequenceEditorToolkit* LevelSequenceEditor = static_cast<ILevelSequenceEditorToolkit*>(AssetEditor);
		TWeakPtr<ISequencer> WeakSequencer = LevelSequenceEditor ? LevelSequenceEditor->GetSequencer() : nullptr;

		if (WeakSequencer.IsValid())
		{
			TArray<TWeakObjectPtr<AActor> > ActorsToAdd;
			ActorsToAdd.Add(MeshActor);
			TArray<FGuid> ActorTracks = WeakSequencer.Pin()->AddActors(ActorsToAdd, false);
			FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));

			for (FGuid ActorTrackGuid : ActorTracks)
			{
				//Delete binding from default animating rig
				FGuid CompGuid = WeakSequencer.Pin()->FindObjectId(*(MeshActor->GetSkeletalMeshComponent()), WeakSequencer.Pin()->GetFocusedTemplateID());
				if (CompGuid.IsValid())
				{
					if (ControlRigEditMode)
					{
						UMovieSceneControlRigParameterTrack* Track = Cast<UMovieSceneControlRigParameterTrack>(MovieScene->FindTrack(UMovieSceneControlRigParameterTrack::StaticClass(), CompGuid, NAME_None));
						if (Track && Track->GetControlRig())
						{
							ControlRigEditMode->RemoveControlRig(Track->GetControlRig());
						}
					}
					if (!MovieScene->RemovePossessable(CompGuid))
					{
						MovieScene->RemoveSpawnable(CompGuid);
					}
				}

				UMovieSceneControlRigParameterTrack* Track = Cast<UMovieSceneControlRigParameterTrack>(MovieScene->FindTrack(UMovieSceneControlRigParameterTrack::StaticClass(), ActorTrackGuid));
				if (!Track)
				{
					Track = MovieScene->AddTrack<UMovieSceneControlRigParameterTrack>(ActorTrackGuid);
				}

				UControlRig* ControlRig = Track->GetControlRig();

				FString ObjectName = (ControlRigClass.GetName());
				if (!ControlRig || !ControlRigClass.IsSourceOf(ControlRig))
				{
					USkeletalMesh* SkeletalMesh = MeshActor->GetSkeletalMeshComponent()->GetSkeletalMeshAsset();
					USkeleton* Skeleton = SkeletalMesh->GetSkeleton();

					ObjectName.RemoveFromEnd(TEXT("_C"));

					// This is either a UControlRig or a UModularRig
					ControlRig = Cast<UControlRig>(ControlRigClass.CreateInstance(Track, FName(*ObjectName), RF_Transactional));
					ControlRig->SetObjectBinding(MakeShared<FControlRigObjectBinding>());
					ControlRig->GetObjectBinding()->BindToObject(MeshActor->GetSkeletalMeshComponent());
					ControlRig->GetDataSourceRegistry()->RegisterDataSource(UControlRig::OwnerComponent, ControlRig->GetObjectBinding()->GetBoundObject());
					ControlRig->Initialize();
					ControlRig->Evaluate_AnyThread();
					ControlRig->CreateRigControlsForCurveContainer();

					WeakSequencer.Pin()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
				}

				UMovieSceneSection* Section = Track->GetAllSections().Num() ? Track->GetAllSections()[0] : nullptr;
				if (!Section)
				{
					Track->Modify();
					Section = Track->CreateControlRigSection(0, ControlRig, true);
					//mz todo need to have multiple rigs with same class
					Track->SetTrackName(FName(*ObjectName));
					Track->SetDisplayName(FText::FromString(ObjectName));

					WeakSequencer.Pin()->EmptySelection();
					WeakSequencer.Pin()->SelectSection(Section);
					WeakSequencer.Pin()->ThrobSectionSelection();
					WeakSequencer.Pin()->ObjectImplicitlyAdded(ControlRig);
				}

				WeakSequencer.Pin()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
				if (!ControlRigEditMode)
				{
					GLevelEditorModeTools().ActivateMode(FControlRigEditMode::ModeName);
					ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
				}
				if (ControlRigEditMode)
				{
					ControlRigEditMode->AddControlRigObject(ControlRig, WeakSequencer.Pin());
				}
			}
		}
	});
}

const TArray<FText>& FControlRigAssetActions::GetSubMenus() const
{
	static const TArray<FText> SubMenus
	{
		LOCTEXT("AnimControlRigSubMenu", "Control Rig")
	};
	return SubMenus;
}

#undef LOCTEXT_NAMESPACE

