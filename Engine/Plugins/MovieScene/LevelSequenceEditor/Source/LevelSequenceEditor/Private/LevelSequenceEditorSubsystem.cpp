// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSequenceEditorSubsystem.h"
#include "Compilation/MovieSceneCompiledDataManager.h"
#include "MVVM/ViewModels/SequencerEditorViewModel.h"
#include "Scripting/SequencerModuleScriptingLayer.h"
#include "SequencerCurveEditorObject.h"
#include "Evaluation/MovieScenePlayback.h"
#include "ILevelSequenceModule.h"
#include "ISequencerModule.h"
#include "LevelSequence.h"
#include "ISceneOutliner.h"
#include "MovieScenePossessable.h"
#include "SequenceBindingTree.h"
#include "MovieScene.h"
#include "MovieSceneSpawnable.h"
#include "SequencerUtilities.h"
#include "Selection.h"
#include "Toolkits/AssetEditorToolkit.h"

#include "PropertyEditorModule.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Modules/ModuleManager.h"

#include "Camera/CameraComponent.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/ArrayBuilder.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/LevelSequenceEditorSettings.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBox.h"
#include "MovieSceneToolHelpers.h"
#include "KeyParams.h"
#include "KeyPropertyParams.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneBindingLifetimeSection.h"
#include "Tracks/IMovieSceneTransformOrigin.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Tracks/MovieSceneBindingLifetimeTrack.h"
#include "Bindings/MovieSceneSpawnableBindingCustomization.h"
#include "Bindings/MovieSceneSpawnableBinding.h"
#include "Bindings/MovieSceneSpawnableActorBinding.h"
#include "Bindings/MovieSceneSpawnableActorBindingCustomization.h"
#include "Bindings/MovieSceneCustomBinding.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "Widgets/Input/SComboBox.h"
#include "DetailLayoutBuilder.h"
#include "IPropertyTypeCustomization.h"
#include "IPropertyUtilities.h"
#include "MovieSceneDynamicBindingCustomization.h"
#include "Conditions/MovieSceneConditionCustomization.h"
#include "Conditions/MovieSceneDirectorBlueprintConditionCustomization.h"
#include "MVVM/Selection/Selection.h"
#include "UnrealEdGlobals.h"
#include "Misc/FeedbackContext.h"
#include "Variants/MovieSceneTimeWarpVariant.h"
#include "Variants/MovieSceneTimeWarpGetter.h"
#include "UObject/UObjectIterator.h"
#include "ObjectTools.h"
#include "Bindings/MovieSceneSpawnableDirectorBlueprintBinding.h"
#include "Bindings/MovieSceneReplaceableDirectorBlueprintBinding.h"
#include "Bindings/MovieSceneReplaceableActorBinding.h"
#include "AssetToolsModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "MovieSceneSequencePlayer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LevelSequenceEditorSubsystem)

DEFINE_LOG_CATEGORY(LogLevelSequenceEditor);

#define LOCTEXT_NAMESPACE "LevelSequenceEditor"

namespace UE::Sequencer
{
	struct FProxyObjectBindingIDPicker : FMovieSceneObjectBindingIDPicker
	{
		using FMovieSceneObjectBindingIDPicker::GetPickerMenu;
		using FMovieSceneObjectBindingIDPicker::GetCurrentItemWidget;

		FProxyObjectBindingIDPicker(TSharedPtr<ISequencer> InSequencer, const FGuid& InObjectBindingID, TFunction<void(FMovieSceneObjectBindingID)> InPreOnPicked = nullptr)
			: FMovieSceneObjectBindingIDPicker(InSequencer->GetFocusedTemplateID(), InSequencer)
			, ObjectBindingID(InObjectBindingID)
			, PreOnPicked(InPreOnPicked)
		{
			Initialize();
			
			InitializeExistingBindingIDs();
		}

		void GatherExistingBindingIDs(UMovieSceneSequence* InSequence, FMovieSceneSequenceID InSequenceID)
		{
			if (UMovieScene* MovieScene = InSequence->GetMovieScene())
			{
				for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); ++Index)
				{
					FMovieScenePossessable& Possessable = MovieScene->GetPossessable(Index);

					if (Possessable.GetSpawnableObjectBindingID().GetGuid() == ObjectBindingID)
					{
						UE::MovieScene::FFixedObjectBindingID BindingID(Possessable.GetGuid(), InSequenceID);
						ExistingBindingIDs.Add(BindingID);
					}
				}
			}
		}

		void InitializeExistingBindingIDs()
		{
			TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
			if (!Sequencer)
			{
				return;
			}

			const FMovieSceneRootEvaluationTemplateInstance& RootInstance = Sequencer->GetEvaluationTemplate();
			const FMovieSceneSequenceHierarchy* Hierarchy = RootInstance.GetCompiledDataManager()->FindHierarchy(RootInstance.GetCompiledDataID());
			if (!Hierarchy)
			{
				return;
			}

			GatherExistingBindingIDs(Sequencer->GetRootMovieSceneSequence(), MovieSceneSequenceID::Root);

			for (const TTuple<FMovieSceneSequenceID, FMovieSceneSubSequenceData>& Pair : Hierarchy->AllSubSequenceData())
			{
				if (UMovieSceneSequence* Sequence = Pair.Value.GetSequence())
				{
					GatherExistingBindingIDs(Sequence, Pair.Key);
				}
			}
		}

		virtual UMovieSceneSequence* GetSequence() const override
		{
			return WeakSequencer.Pin() ? WeakSequencer.Pin()->GetFocusedMovieSceneSequence() : nullptr;
		}
		virtual void SetCurrentValue(const FMovieSceneObjectBindingID& InBindingId) override
		{
			FScopedTransaction Transaction(LOCTEXT("ChangeBindingProxyTransaction", "Change Proxy Binding"));

			if (UMovieSceneSequence* Sequence = GetSequence())
			{
				if (PreOnPicked)
				{
					PreOnPicked(InBindingId);
				}

				FMovieScenePossessable* Possessable = Sequence->GetMovieScene()->FindPossessable(ObjectBindingID);
				if (Possessable)
				{
					Possessable->SetSpawnableObjectBindingID(InBindingId);
				}
			}
		}

		virtual FMovieSceneObjectBindingID GetCurrentValue() const override
		{
			if (UMovieSceneSequence* Sequence = GetSequence())
			{
				FMovieScenePossessable* Possessable = Sequence->GetMovieScene()->FindPossessable(ObjectBindingID);
				if (Possessable)
				{
					return Possessable->GetSpawnableObjectBindingID();
				}
			}
			return FMovieSceneObjectBindingID();
		}

		bool IsNodeAllowed(const TSharedPtr<FSequenceBindingNode>& Node) const override
		{
			// Disallow picking a binding from this current sequence
			const UE::MovieScene::FFixedObjectBindingID &BindingID = Node->BindingID;
			if (BindingID.SequenceID == LocalSequenceID)
			{
				return false;
			}

			// Disallow picking a binding that already is a proxy binding to this node
			if (ExistingBindingIDs.Contains(Node->BindingID))
			{
				return false;
			}

			return FMovieSceneObjectBindingIDPicker::IsNodeAllowed(Node);
		}

	private:
		FGuid ObjectBindingID;
		TFunction<void(FMovieSceneObjectBindingID)> PreOnPicked;
		TArray<UE::MovieScene::FFixedObjectBindingID> ExistingBindingIDs;
	};
}

namespace UE::LevelSequenceEditor::Private
{
	FTransform GetTransformOrigin(TSharedRef<ISequencer> Sequencer)
	{
		FTransform TransformOrigin;

		const IMovieScenePlaybackClient* Client = Sequencer->GetPlaybackClient();
		const UObject* InstanceData = Client ? Client->GetInstanceData() : nullptr;
		const IMovieSceneTransformOrigin* RawInterface = Cast<const IMovieSceneTransformOrigin>(InstanceData);

		const bool bHasInterface = RawInterface || (InstanceData && InstanceData->GetClass()->ImplementsInterface(UMovieSceneTransformOrigin::StaticClass()));
		if (bHasInterface)
		{
			// Retrieve the current origin
			TransformOrigin = RawInterface ? RawInterface->GetTransformOrigin() : IMovieSceneTransformOrigin::Execute_BP_GetTransformOrigin(InstanceData);
		}

		return TransformOrigin;
	}
}

class FMovieSceneBindingPropertyInfoListCustomization : public IDetailCustomization
{
public:

	FMovieSceneBindingPropertyInfoListCustomization(TWeakPtr<ISequencer> InSequencer, UMovieScene* InMovieScene, FGuid InBindingGuid, ULevelSequenceEditorSubsystem* InLevelSequenceEditorSubsystem);

	static TSharedRef<IDetailCustomization> MakeInstance(TWeakPtr<ISequencer> InSequencer, UMovieScene* InMovieScene, FGuid InBindingGuid, ULevelSequenceEditorSubsystem* InLevelSequenceEditorSubsystem);
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;


private:

	TSharedRef<SWidget> OnGetConvertMenuContent(IDetailLayoutBuilder& DetailBuilder);

	TSharedRef<SWidget> GenerateBindingTypePicker(IDetailLayoutBuilder& DetailBuilder);
	const FSlateBrush* GetBindingTypeIcon() const;
	FText GetBindingTypeValueAsString() const;

	TWeakPtr<ISequencer> SequencerPtr;
	TObjectPtr<UMovieScene> MovieScene = nullptr;
	TObjectPtr<ULevelSequenceEditorSubsystem> LevelSequenceEditorSubsystem;
	FGuid BindingGuid;
	TObjectPtr<UMovieSceneBindingPropertyInfoList> BindingList;

	TArray<TSharedPtr<FText>> BindingTypeNames;
};

FMovieSceneBindingPropertyInfoListCustomization::FMovieSceneBindingPropertyInfoListCustomization(TWeakPtr<ISequencer> InSequencer, UMovieScene* InMovieScene, FGuid InBindingGuid, ULevelSequenceEditorSubsystem* InLevelSequenceEditorSubsystem)
	: SequencerPtr(InSequencer)
	, MovieScene(InMovieScene)
	, LevelSequenceEditorSubsystem(InLevelSequenceEditorSubsystem)
	, BindingGuid(InBindingGuid)
{

}

TSharedRef<IDetailCustomization> FMovieSceneBindingPropertyInfoListCustomization::MakeInstance(TWeakPtr<ISequencer> InSequencer, UMovieScene* InMovieScene, FGuid InBindingGuid, ULevelSequenceEditorSubsystem* InLevelSequenceEditorSubsystem)
{
	return MakeShareable(new FMovieSceneBindingPropertyInfoListCustomization(InSequencer, InMovieScene, InBindingGuid, InLevelSequenceEditorSubsystem));
}

void FMovieSceneBindingPropertyInfoListCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& SectionCategory = DetailBuilder.EditCategory("Binding Properties", FText(), ECategoryPriority::Important);

	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	TSharedPtr<ISequencer> Sequencer = SequencerPtr.Pin();

	if (Objects.Num() > 0)
	{
		BindingList = Cast<UMovieSceneBindingPropertyInfoList>(Objects[0].Get());
		if (BindingList)
		{
			if (Sequencer.IsValid())
			{
				UMovieSceneSequence* Sequence = MovieScene->GetTypedOuter<UMovieSceneSequence>();
				if (BindingList->Bindings.Num() > 0)
				{
					// Grab the first one- we guarantee the binding types are the same.
					bool bShowConvert = true;
					if (FMovieScenePossessable* Possessable = MovieScene->FindPossessable(BindingGuid))
					{
						if (Possessable && Possessable->GetParent().IsValid())
						{
							bShowConvert = false;
						}
					}
					bool bHasBoundObject = false;
					TArray<UObject*> BoundObjects = MovieSceneHelpers::GetBoundObjects(Sequencer->GetFocusedTemplateID(), BindingGuid, Sequencer->GetSharedPlaybackState(), 0);
					for (UObject* BoundObject : BoundObjects)
					{
						if (BoundObject)
						{
							bHasBoundObject = true;
							break;
						}
					}

					FDetailWidgetRow& BindingTypeRow = SectionCategory.AddCustomRow(FText::GetEmpty());
					BindingTypeRow.NameContent()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("BindingPropertyType", "Binding Type"))
						.ToolTipText(LOCTEXT("BindingPropertyType_Tooltip", "The type of binding for this object binding track entry"))
						.Font(IDetailLayoutBuilder::GetDetailFont())
						];
					BindingTypeRow.ValueContent()
						[
							SNew(SComboButton)
							.OnGetMenuContent(FOnGetContent::CreateLambda([this, &DetailBuilder]() { return GenerateBindingTypePicker(DetailBuilder);}))
							.ContentPadding(0.0f)
							.ButtonContent()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(0.0f, 0.0f, 4.0f, 0.0f)
								[
									SNew(SImage)
									.Image(this, &FMovieSceneBindingPropertyInfoListCustomization::GetBindingTypeIcon)
								]
							+ SHorizontalBox::Slot()
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text(this, &FMovieSceneBindingPropertyInfoListCustomization::GetBindingTypeValueAsString)
								]
							]
							.IsEnabled(!bHasBoundObject)
						];

					// Only show certain menus if we have a currently bound object
					if (bShowConvert && bHasBoundObject)
					{
						FDetailWidgetRow& ConvertToRow = SectionCategory.AddCustomRow(LOCTEXT("ConvertBindingTo", "Convert Binding(s) To..."));
						ConvertToRow.WholeRowContent()
							[
								SNew(SComboButton)
								.OnGetMenuContent(FOnGetContent::CreateLambda([this, &DetailBuilder]() { return OnGetConvertMenuContent(DetailBuilder); }))
							.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
							.ButtonContent()
							[
								SNew(STextBlock).Text(LOCTEXT("ConvertBindingTo", "Convert Binding(s) To..."))
							]
							];
					}
				}
			}
		}
	}
}

TSharedRef<SWidget> FMovieSceneBindingPropertyInfoListCustomization::GenerateBindingTypePicker(IDetailLayoutBuilder& DetailBuilder)
{
	FMenuBuilder MenuBuilder(true, nullptr, nullptr, true);
	if (SequencerPtr.IsValid())
	{
		TSharedRef<ISequencer> Sequencer = SequencerPtr.Pin().ToSharedRef();

		TArray<FSequencerChangeBindingInfo> Bindings;
		for (int32 BindingIndex = 0; BindingIndex < BindingList->Bindings.Num(); ++BindingIndex)
		{
			Bindings.Add({ BindingGuid, BindingIndex });
		}
		LevelSequenceEditorSubsystem->AddChangeBindingTypeMenu(MenuBuilder, Sequencer, Bindings, false, [this, &DetailBuilder]()
			{
				if (TSharedPtr<IDetailsView> DetailsView  = DetailBuilder.GetDetailsViewSharedPtr())
				{
					if (TSharedPtr<ISequencer> Sequencer = SequencerPtr.Pin())
					{
						LevelSequenceEditorSubsystem->RefreshBindingDetails(Sequencer.ToSharedRef(), DetailsView.Get(), BindingGuid);
						LevelSequenceEditorSubsystem->OnFinishedChangingLocators(FPropertyChangedEvent(nullptr), Sequencer.ToSharedRef(), DetailsView.ToSharedRef(), BindingGuid);
					}
				}
			});
	}
	return MenuBuilder.MakeWidget();
}

FText FMovieSceneBindingPropertyInfoListCustomization::GetBindingTypeValueAsString() const
{
	TSharedPtr<ISequencer> Sequencer = SequencerPtr.Pin();
	if (BindingList)
	{
		if (Sequencer.IsValid())
		{
			UMovieSceneSequence* Sequence = MovieScene->GetTypedOuter<UMovieSceneSequence>();
			if (BindingList->Bindings.Num() > 0)
			{
				// All bindings will be the same type
				if (!BindingList->Bindings[0].CustomBinding)
				{
					// Possessable
					return LOCTEXT("BindingType_Possessable", "Possessable");
				}
				else
				{
					 return BindingList->Bindings[0].CustomBinding->GetBindingTypePrettyName();
				}
			}
		}
	}
	return FText();
}

const FSlateBrush* FMovieSceneBindingPropertyInfoListCustomization::GetBindingTypeIcon() const
{
	TSharedPtr<ISequencer> Sequencer = SequencerPtr.Pin();
	if (BindingList)
	{
		if (Sequencer.IsValid())
		{
			UMovieSceneSequence* Sequence = MovieScene->GetTypedOuter<UMovieSceneSequence>();
			if (BindingList->Bindings.Num() > 0)
			{
				// All bindings will be the same type
				if (!BindingList->Bindings[0].CustomBinding)
				{
					// Possessable
					return nullptr;
				}
				else
				{
					return BindingList->Bindings[0].CustomBinding->GetBindingTrackCustomIconOverlay().GetIcon();
				}
			}
		}
	}

	return nullptr;
}

TSharedRef<SWidget> FMovieSceneBindingPropertyInfoListCustomization::OnGetConvertMenuContent(IDetailLayoutBuilder& DetailBuilder)
{
	FMenuBuilder MenuBuilder(true, nullptr, nullptr, true);


	if (SequencerPtr.IsValid() && BindingList)
	{
		TSharedRef<ISequencer> Sequencer = SequencerPtr.Pin().ToSharedRef();
		TArray<FSequencerChangeBindingInfo> Bindings;
		for (int32 BindingIndex = 0; BindingIndex < BindingList->Bindings.Num(); ++BindingIndex)
		{
			Bindings.Add({ BindingGuid, BindingIndex });
		}

		LevelSequenceEditorSubsystem->AddChangeBindingTypeMenu(MenuBuilder, Sequencer, Bindings, true, [this, &DetailBuilder]()
			{
				if (TSharedPtr<IDetailsView> DetailsView = DetailBuilder.GetDetailsViewSharedPtr())
				{
					if (TSharedPtr<ISequencer> Sequencer = SequencerPtr.Pin())
					{
						LevelSequenceEditorSubsystem->RefreshBindingDetails(Sequencer.ToSharedRef(), DetailsView.Get(), BindingGuid);
					}
				}
			});
	}
	return MenuBuilder.MakeWidget();
}


class FMovieSceneBindingPropertyInfoDetailCustomization : public IPropertyTypeCustomization
{
public:
	FMovieSceneBindingPropertyInfoDetailCustomization(TWeakPtr<ISequencer> InSequencer, UMovieScene* InMovieScene, FGuid InBindingGuid, ULevelSequenceEditorSubsystem* InLevelSequenceEditorSubsystem);

	// Begin IDetailCustomization interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ End IDetailCustomization interface

private:

	TSharedRef<SWidget> OnGetChangeClassMenuContent(IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils);

	TWeakPtr<ISequencer> SequencerPtr;
	TObjectPtr<UMovieScene> MovieScene = nullptr;
	TObjectPtr<ULevelSequenceEditorSubsystem> LevelSequenceEditorSubsystem;
	FGuid BindingGuid;
	int32 BindingIndex = 0;

	TSharedPtr<IPropertyHandle> StructPropertyHandle;
	TSharedPtr<UE::Sequencer::FProxyObjectBindingIDPicker> ProxyPicker;
};

FMovieSceneBindingPropertyInfoDetailCustomization::FMovieSceneBindingPropertyInfoDetailCustomization(TWeakPtr<ISequencer> InSequencer, UMovieScene* InMovieScene, FGuid InBindingGuid, ULevelSequenceEditorSubsystem* InLevelSequenceEditorSubsystem)
	: SequencerPtr(InSequencer)
	, MovieScene(InMovieScene)
	, LevelSequenceEditorSubsystem(InLevelSequenceEditorSubsystem)
	, BindingGuid(InBindingGuid)
{

}

void FMovieSceneBindingPropertyInfoDetailCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	HeaderRow.ShouldAutoExpand(true);

	HeaderRow.NameContent()
		[
			InStructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			InStructPropertyHandle->CreatePropertyValueWidget()
		];

	StructPropertyHandle = InStructPropertyHandle;
}

void FMovieSceneBindingPropertyInfoDetailCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<ISequencer> Sequencer = SequencerPtr.Pin();
	if (Sequencer.IsValid() && InStructPropertyHandle->IsValidHandle())
	{
		UMovieSceneSequence* Sequence = MovieScene->GetTypedOuter<UMovieSceneSequence>();
		TArray<void*> StructPtrs;
		StructPropertyHandle->AccessRawData(StructPtrs);
		FMovieSceneBindingPropertyInfo* BindingPropertyInfo = (StructPtrs.Num() == 1) ? reinterpret_cast<FMovieSceneBindingPropertyInfo*>(StructPtrs[0]) : nullptr;
		if (BindingPropertyInfo)
		{
			BindingIndex = InStructPropertyHandle->GetArrayIndex();

			// Show Change class and save default state menus for spawnables
			if (MovieSceneHelpers::SupportsObjectTemplate(Sequence, BindingGuid, Sequencer->GetSharedPlaybackState(), BindingIndex))
			{
				FDetailWidgetRow& ChangeClassRow = StructBuilder.AddCustomRow(LOCTEXT("ChangeClass", "Change Class..."));
				ChangeClassRow.WholeRowContent()
					[
						SNew(SComboButton)
						.OnGetMenuContent(FOnGetContent::CreateLambda([this, &StructBuilder, &CustomizationUtils]() { return OnGetChangeClassMenuContent(StructBuilder, CustomizationUtils); }))
					.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
					.ButtonContent()
					[
						SNew(STextBlock).Text(LOCTEXT("ChangeClass", "Change Class..."))
					]
					];

				// Save Default State
				FDetailWidgetRow& SaveDefaultStateRow = StructBuilder.AddCustomRow(LOCTEXT("SaveDefaultState", "Save Default State"));
				SaveDefaultStateRow.WholeRowContent()
					[
						SNew(SButton)
						.Text(LOCTEXT("SaveDefaultState", "Save Default State"))
					.ToolTipText(LOCTEXT("SaveDefaultState_Tooltip", "Save the current state of this spawnable as default properties"))
					.OnClicked_Lambda([this]()
						{
							TSharedPtr<ISequencer> Sequencer = SequencerPtr.Pin();
							if (Sequencer.IsValid())
							{
								Sequencer->GetSpawnRegister().SaveDefaultSpawnableState(BindingGuid, BindingIndex, Sequencer->GetFocusedTemplateID(), Sequencer->GetSharedPlaybackState());
							}
							return FReply::Handled();
						})
					];
			}

			if (!BindingPropertyInfo->CustomBinding)
			{
				FMovieScenePossessable* Possessable = Sequence->GetMovieScene()->FindPossessable(BindingGuid);
				if (Possessable && Possessable->GetSpawnableObjectBindingID().IsValid())
				{
					ProxyPicker = MakeShared<UE::Sequencer::FProxyObjectBindingIDPicker>(Sequencer, this->BindingGuid);

					StructBuilder.AddCustomRow(FText())
					.NameContent()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ProxyLabel", "Proxy Binding"))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
					.ValueContent()
					[
						SNew(SComboButton)
						.OnGetMenuContent(ProxyPicker.ToSharedRef(), &UE::Sequencer::FProxyObjectBindingIDPicker::GetPickerMenu)
						.ContentPadding(FMargin(4.0, 2.0))
						.ButtonContent()
						[
							ProxyPicker->GetCurrentItemWidget(
								SNew(STextBlock)
								.Font(CustomizationUtils.GetRegularFont())
							)
						]
					];
				}
				else
				{
					// Show locator property
					TSharedPtr<IPropertyHandle> LocatorProperty = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMovieSceneBindingPropertyInfo, Locator));
					if (LocatorProperty.IsValid())
					{
						StructBuilder.AddProperty(LocatorProperty.ToSharedRef());
					}
				}
			}
			else
			{
				// Show instanced binding type property
				TSharedPtr<IPropertyHandle> CustomBindingProperty = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMovieSceneBindingPropertyInfo, CustomBinding));
				if (CustomBindingProperty.IsValid())
				{
					StructBuilder.AddProperty(CustomBindingProperty.ToSharedRef()).CustomWidget(true)
						.NameContent()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("BindingProperties", "Binding Properties"))
							.Font(IDetailLayoutBuilder::GetDetailFont())
						];
				}
			}
		}
	}
}


TSharedRef<SWidget> FMovieSceneBindingPropertyInfoDetailCustomization::OnGetChangeClassMenuContent(IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	FMenuBuilder MenuBuilder(true, nullptr, nullptr, true);

	TSharedPtr<ISequencer> Sequencer = SequencerPtr.Pin();
	if (Sequencer.IsValid() && StructPropertyHandle->IsValidHandle())
	{
		TArray<FSequencerChangeBindingInfo> Bindings;
		Bindings.Add(FSequencerChangeBindingInfo(BindingGuid, BindingIndex));

		FSequencerUtilities::AddChangeClassMenu(MenuBuilder, Sequencer.ToSharedRef(), Bindings, [this, &StructBuilder, &CustomizationUtils]()
			{
				if (TSharedPtr<IDetailsView> DetailsView = StructBuilder.GetParentCategory().GetParentLayout().GetDetailsViewSharedPtr())
				{
					if (TSharedPtr<ISequencer> Sequencer = SequencerPtr.Pin())
					{
						LevelSequenceEditorSubsystem->RefreshBindingDetails(Sequencer.ToSharedRef(), DetailsView.Get(), BindingGuid);
					}
				}
			});
	}
	return MenuBuilder.MakeWidget();
}

void ULevelSequenceEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	UE_LOGF(LogLevelSequenceEditor, Log, "LevelSequenceEditor subsystem initialized.");

	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	OnSequencerCreatedHandle = SequencerModule.RegisterOnSequencerCreated(FOnSequencerCreated::FDelegate::CreateUObject(this, &ULevelSequenceEditorSubsystem::OnSequencerCreated));

	/* Menu extenders */
	RebindComponentMenuExtender = MakeShareable(new FExtender);
	RebindComponentMenuExtender->AddMenuExtension("Possessable", EExtensionHook::First, nullptr, FMenuExtensionDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder) {
		// Only add menu entries where the focused sequence is a ULevelSequence
		if (!GetActiveSequencer())
		{
			return;
		}

		TArray<FName> ComponentNames;
		GetRebindComponentNames(ComponentNames);
		if (ComponentNames.Num() > 0)
		{
			FFormatNamedArguments Args;
			MenuBuilder.AddSubMenu(
				FText::Format(LOCTEXT("RebindComponent", "Rebind Component"), Args),
				FText::Format(LOCTEXT("RebindComponentTooltip", "Rebind component by moving the tracks from one component to another component."), Args),
				FNewMenuDelegate::CreateLambda([this](FMenuBuilder& SubMenuBuilder) { RebindComponentMenu(SubMenuBuilder); }));
		}
		}));

	SequencerModule.GetObjectBindingContextMenuExtensibilityManager()->AddExtender(RebindComponentMenuExtender);

	SidebarMenuExtender = MakeShared<FExtender>();

	SidebarMenuExtender->AddMenuExtension(TEXT("TrackRowMetadata"), EExtensionHook::First, nullptr,
		FMenuExtensionDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder)
			{
				AddTrackRowMetadataMenu(MenuBuilder);
			}));

	SequencerModule.GetSidebarExtensibilityManager()->AddExtender(SidebarMenuExtender);
}

void ULevelSequenceEditorSubsystem::Deinitialize()
{
	UE_LOGF(LogLevelSequenceEditor, Log, "LevelSequenceEditor subsystem deinitialized.");

	ISequencerModule* SequencerModulePtr = FModuleManager::Get().GetModulePtr<ISequencerModule>("Sequencer");
	if (SequencerModulePtr)
	{
		SequencerModulePtr->UnregisterOnSequencerCreated(OnSequencerCreatedHandle);
	}

	BindingPropertyInfoLists.Empty();
	TrackRowMetadataHelperList.Empty();
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnMenuBeingDestroyed().RemoveAll(this);
	}

}

void ULevelSequenceEditorSubsystem::OnSequencerCreated(TSharedRef<ISequencer> InSequencer)
{
	UE_LOGF(LogLevelSequenceEditor, VeryVerbose, "ULevelSequenceEditorSubsystem::OnSequencerCreated");

	Sequencers.Add(TWeakPtr<ISequencer>(InSequencer));
	InSequencer->OnCloseEvent().AddUObject(this, &ULevelSequenceEditorSubsystem::OnSequencerClosed);

	CreatePlaybackDelegates(InSequencer);

	// Create a new binding list that will be used with the sequencer that was just added
	UMovieSceneBindingPropertyInfoList* BindingPropertyInfoList = GetBindingPropertyInfoList(InSequencer);
	if (!BindingPropertyInfoList)
	{
		UMovieSceneBindingPropertyInfoList* NewBindingPropertyInfoList = NewObject<UMovieSceneBindingPropertyInfoList>(this);
		NewBindingPropertyInfoList->Sequencer = InSequencer;
		BindingPropertyInfoLists.Add(NewBindingPropertyInfoList);
	}
}

void ULevelSequenceEditorSubsystem::OnSequencerClosed(TSharedRef<ISequencer> InSequencer)
{
	const uint32 IndexToRemove = BindingPropertyInfoLists.IndexOfByPredicate([InSequencer](const TObjectPtr<UMovieSceneBindingPropertyInfoList>& List) { return List->Sequencer == InSequencer; });
	if (IndexToRemove != INDEX_NONE)
	{
		BindingPropertyInfoLists.RemoveAt(IndexToRemove);
	}

	if (FSequencerPlaybackDelegateHandles* Handles = SequencerPlaybackDelegateHandles.Find(InSequencer))
	{
		InSequencer->OnGlobalTimeChanged().Remove(Handles->GlobalTimeChanged);
		InSequencer->OnPlayEvent().Remove(Handles->PlayEvent);
		InSequencer->OnStopEvent().Remove(Handles->StopEvent);
		InSequencer->OnRecordEvent().Remove(Handles->RecordEvent);
		InSequencer->OnBeginScrubbingEvent().Remove(Handles->BeginScrubbing);
		InSequencer->OnEndScrubbingEvent().Remove(Handles->EndScrubbing);
		SequencerPlaybackDelegateHandles.Remove(InSequencer);
	}

	TrackRowMetadataHelperList.Empty();
}

void ULevelSequenceEditorSubsystem::CreatePlaybackDelegates(TSharedRef<ISequencer> InSequencer)
{
	FSequencerPlaybackDelegateHandles Handles;
	Handles.GlobalTimeChanged = InSequencer->OnGlobalTimeChanged().AddUObject(this, &ULevelSequenceEditorSubsystem::HandleGlobalTimeChanged);
	Handles.PlayEvent         = InSequencer->OnPlayEvent().AddUObject(this, &ULevelSequenceEditorSubsystem::HandlePlayEvent);
	Handles.StopEvent         = InSequencer->OnStopEvent().AddUObject(this, &ULevelSequenceEditorSubsystem::HandleStopEvent);
	Handles.RecordEvent       = InSequencer->OnRecordEvent().AddUObject(this, &ULevelSequenceEditorSubsystem::HandleRecordEvent);
	Handles.BeginScrubbing    = InSequencer->OnBeginScrubbingEvent().AddUObject(this, &ULevelSequenceEditorSubsystem::HandleBeginScrubbingEvent);
	Handles.EndScrubbing      = InSequencer->OnEndScrubbingEvent().AddUObject(this, &ULevelSequenceEditorSubsystem::HandleEndScrubbingEvent);
	SequencerPlaybackDelegateHandles.Add(InSequencer, Handles);
}

void ULevelSequenceEditorSubsystem::HandleGlobalTimeChanged() 
{
	OnGlobalTimeChanged.Broadcast(); 
}

void ULevelSequenceEditorSubsystem::HandlePlayEvent() 
{
	OnPlayEvent.Broadcast(); 
}

void ULevelSequenceEditorSubsystem::HandleStopEvent()  
{
	OnStopEvent.Broadcast(); 
}
void ULevelSequenceEditorSubsystem::HandleRecordEvent()  
{ 
	OnRecordEvent.Broadcast(); 
}
void ULevelSequenceEditorSubsystem::HandleBeginScrubbingEvent() 
{
	OnBeginScrubbingEvent.Broadcast(); 
}

void ULevelSequenceEditorSubsystem::HandleEndScrubbingEvent()   
{ 
	OnEndScrubbingEvent.Broadcast(); 
}

void ULevelSequenceEditorSubsystem::AddBindingDetailCustomizations(TSharedRef<IDetailsView> DetailsView, TSharedPtr<ISequencer> ActiveSequencer, FGuid BindingGuid)
{
	// TODO: Do we want to create a generalized way for folks to add instanced property layouts for other custom binding types so they can have access to sequencer context?
	if (ActiveSequencer.IsValid())
	{
		UMovieSceneSequence* Sequence = ActiveSequencer->GetFocusedMovieSceneSequence();
		UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
		if (MovieScene)
		{
			FPropertyEditorModule& PropertyEditor = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

			DetailsView->RegisterInstancedCustomPropertyLayout(UMovieSceneBindingPropertyInfoList::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FMovieSceneBindingPropertyInfoListCustomization::MakeInstance, ActiveSequencer.ToWeakPtr(), MovieScene, BindingGuid, this));
			
			DetailsView->RegisterInstancedCustomPropertyTypeLayout(FMovieSceneBindingPropertyInfo::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateLambda([](TWeakPtr<ISequencer> InSequencer, UMovieScene* InMovieScene, FGuid InBindingGuid, ULevelSequenceEditorSubsystem* LevelSequenceEditorSubsystem)
				{
					return MakeShared<FMovieSceneBindingPropertyInfoDetailCustomization>(InSequencer, InMovieScene, InBindingGuid, LevelSequenceEditorSubsystem);
				}, ActiveSequencer.ToWeakPtr(), MovieScene, BindingGuid, this));

			DetailsView->RegisterInstancedCustomPropertyTypeLayout(FMovieSceneDynamicBinding::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FMovieSceneDynamicBindingCustomization::MakeInstance, MovieScene, BindingGuid, 0));
			
			DetailsView->RegisterInstancedCustomPropertyLayout(UMovieSceneSpawnableActorBinding::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FMovieSceneSpawnableActorBindingBaseCustomization::MakeInstance, ActiveSequencer.ToWeakPtr(), MovieScene, BindingGuid));
		}
	}
}

void ULevelSequenceEditorSubsystem::AddTrackRowMetadataCustomizations(TSharedRef<IDetailsView> DetailsView, TSharedPtr<ISequencer> ActiveSequencer, UMovieSceneSequence* Sequence)
{
	if (ActiveSequencer.IsValid())
	{
		UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
		if (MovieScene)
		{
			// Although we normally customize this type, we need to do it instanced here to pass in the sequence information, as it won't be part of an outer sequence UObject
			DetailsView->RegisterInstancedCustomPropertyTypeLayout("MovieSceneConditionContainer", FOnGetPropertyTypeCustomizationInstance::CreateLambda([Sequence, WeakSequencer=ActiveSequencer.ToWeakPtr()]() {
				return FMovieSceneConditionCustomization::MakeInstance(Sequence, WeakSequencer); }));

			DetailsView->RegisterInstancedCustomPropertyTypeLayout("MovieSceneDirectorBlueprintConditionData", FOnGetPropertyTypeCustomizationInstance::CreateLambda([=]() {
				return FMovieSceneDirectorBlueprintConditionCustomization::MakeInstance(MovieScene); }));
		}
	}
}

void ULevelSequenceEditorSubsystem::OnTrackRowMetadataMenuBeingDestroyed(const TSharedRef<IMenu>& Menu, TSharedRef<IDetailsView> DetailsView)
{
	TSharedPtr<SWidget> ContentWidget = Menu->GetContent();
	TSharedPtr<SWidget> ParentWidget = DetailsView;
	while (ParentWidget)
	{
		if (ParentWidget == ContentWidget)
		{
			// Track Row Metadata menu has closed, clear the metadata helper list
			TrackRowMetadataHelperList.Empty();
			FSlateApplication::Get().OnMenuBeingDestroyed().RemoveAll(this);
			break;
		}
		ParentWidget = ParentWidget->GetParentWidget();
	}
}

TSharedPtr<ISequencer> ULevelSequenceEditorSubsystem::GetActiveSequencer()
{
	for (TWeakPtr<ISequencer> Ptr : Sequencers)
	{
		if (Ptr.IsValid())
		{
			UMovieSceneSequence* Sequence = Ptr.Pin()->GetFocusedMovieSceneSequence();
			if (Sequence && Sequence->IsA<ULevelSequence>())
			{
				return Ptr.Pin();
			}
		}
	}

	return nullptr;
}

USequencerModuleScriptingLayer* ULevelSequenceEditorSubsystem::GetScriptingLayer()
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer)
	{
		return Cast<USequencerModuleScriptingLayer>(Sequencer->GetViewModel()->GetScriptingLayer());
	}
	return nullptr;
}

USequencerCurveEditorObject* ULevelSequenceEditorSubsystem::GetCurveEditor()
{
	TObjectPtr<USequencerCurveEditorObject> CurveEditorObject;
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer)
	{
		TObjectPtr<USequencerCurveEditorObject> *ExistingCurveEditorObject = CurveEditorObjects.Find(Sequencer);
		if (ExistingCurveEditorObject)
		{
			CurveEditorObject = *ExistingCurveEditorObject;
		}
		else
		{
			CurveEditorObject = NewObject<USequencerCurveEditorObject>(this);
			CurveEditorObject->SetSequencer(Sequencer);
			CurveEditorObjects.Add(Sequencer, CurveEditorObject);
			CurveEditorArray.Add(CurveEditorObject);
		}
	}
	return CurveEditorObject;
}

void ULevelSequenceEditorSubsystem::AddDefaultTracksForActor(TSharedRef<ISequencer> InSequencer, AActor& Actor, const FGuid Binding)
{
	static bool bReentrantUpdate = false;
	if (bReentrantUpdate)
	{
		return;
	}

	TGuardValue<bool> ReentrancyGuard(bReentrantUpdate, true);

	// get focused movie scene
	UMovieSceneSequence* Sequence = InSequencer->GetFocusedMovieSceneSequence();

	if (Sequence == nullptr)
	{
		return;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();

	if (MovieScene == nullptr)
	{
		return;
	}


	// Create a default section for a new track.
	//
	// @param InNewTrack the track to create a default section for
	// @param InComponent for MovieScene3DTransformTrack, optional scene component to initialize the relative transform
	auto CreateDefaultTrackSection = [&InSequencer, &Actor](UMovieSceneTrack* InNewTrack, UObject* InComponent)
	{
		// Track class permissions can deny track creation. (UMovieScene::IsTrackClassAllowed)
		if (!InNewTrack)
		{
			return;
		}

#if WITH_EDITORONLY_DATA
		if (!InNewTrack->SupportsDefaultSections())
		{
			return;
		}
#endif

		UMovieSceneSection* NewSection;
		if (InNewTrack->GetAllSections().Num() > 0)
		{
			NewSection = InNewTrack->GetAllSections()[0];
		}
		else
		{
			NewSection = InNewTrack->CreateNewSection();
			InNewTrack->AddSection(*NewSection);
		}

		// @todo sequencer: hack: setting defaults for transform tracks
		if (InNewTrack->IsA(UMovieScene3DTransformTrack::StaticClass()) && InSequencer->GetAutoSetTrackDefaults())
		{
			auto TransformSection = Cast<UMovieScene3DTransformSection>(NewSection);

			FTransform Transform = Actor.GetTransform();

			if (USceneComponent* SceneComponent = Cast<USceneComponent>(InComponent))
			{
				Transform = SceneComponent->GetRelativeTransform();

				if (!SceneComponent->GetAttachParent())
				{
					Transform *= UE::LevelSequenceEditor::Private::GetTransformOrigin(InSequencer).Inverse();
				}
			}

			FVector Location = Transform.GetTranslation();
			FRotator Rotation = Transform.GetRotation().Rotator();
			FVector Scale = Transform.GetScale3D();

			TArrayView<FMovieSceneDoubleChannel*> DoubleChannels = TransformSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
			DoubleChannels[0]->SetDefault(Location.X);
			DoubleChannels[1]->SetDefault(Location.Y);
			DoubleChannels[2]->SetDefault(Location.Z);

			DoubleChannels[3]->SetDefault(Rotation.Euler().X);
			DoubleChannels[4]->SetDefault(Rotation.Euler().Y);
			DoubleChannels[5]->SetDefault(Rotation.Euler().Z);

			DoubleChannels[6]->SetDefault(Scale.X);
			DoubleChannels[7]->SetDefault(Scale.Y);
			DoubleChannels[8]->SetDefault(Scale.Z);
		}

		if (InSequencer->GetInfiniteKeyAreas())
		{
			NewSection->SetRange(TRange<FFrameNumber>::All());
		}
	};

	// add default tracks
	for (const FLevelSequenceTrackSettings& TrackSettings : GetDefault<ULevelSequenceEditorSettings>()->TrackSettings)
	{
		UClass* MatchingActorClass = TrackSettings.MatchingActorClass.ResolveClass();

		if ((MatchingActorClass == nullptr) || !Actor.IsA(MatchingActorClass))
		{
			continue;
		}

		// add tracks by type
		for (const FSoftClassPath& DefaultTrack : TrackSettings.DefaultTracks)
		{
			UClass* TrackClass = DefaultTrack.ResolveClass();

			// exclude any tracks explicitly marked for exclusion
			for (const FLevelSequenceTrackSettings& ExcludeTrackSettings : GetDefault<ULevelSequenceEditorSettings>()->TrackSettings)
			{
				UClass* ExcludeMatchingActorClass = ExcludeTrackSettings.MatchingActorClass.ResolveClass();

				if ((ExcludeMatchingActorClass == nullptr) || !Actor.IsA(ExcludeMatchingActorClass))
				{
					continue;
				}

				for (const FSoftClassPath& ExcludeDefaultTrack : ExcludeTrackSettings.ExcludeDefaultTracks)
				{
					if (ExcludeDefaultTrack == DefaultTrack)
					{
						TrackClass = nullptr;
						break;
					}
				}
			}

			if (TrackClass != nullptr)
			{
				UMovieSceneTrack* NewTrack = MovieScene->FindTrack(TrackClass, Binding);
				if (!NewTrack)
				{
					NewTrack = MovieScene->AddTrack(TrackClass, Binding);
					CreateDefaultTrackSection(NewTrack, Actor.GetRootComponent());
				}
			}
		}

		// construct a map of the properties that should be excluded per component
		TMap<UObject*, TArray<FString> > ExcludePropertyTracksMap;
		for (const FLevelSequenceTrackSettings& ExcludeTrackSettings : GetDefault<ULevelSequenceEditorSettings>()->TrackSettings)
		{
			UClass* ExcludeMatchingActorClass = ExcludeTrackSettings.MatchingActorClass.ResolveClass();

			if ((ExcludeMatchingActorClass == nullptr) || !Actor.IsA(ExcludeMatchingActorClass))
			{
				continue;
			}

			for (const FLevelSequencePropertyTrackSettings& PropertyTrackSettings : ExcludeTrackSettings.ExcludeDefaultPropertyTracks)
			{
				UObject* PropertyOwner = &Actor;

				// determine object hierarchy
				TArray<FString> ComponentNames;
				PropertyTrackSettings.ComponentPath.ParseIntoArray(ComponentNames, TEXT("."));

				for (const FString& ComponentName : ComponentNames)
				{
					PropertyOwner = FindObjectFast<UObject>(PropertyOwner, *ComponentName);

					if (PropertyOwner == nullptr)
					{
						continue;
					}
				}

				if (PropertyOwner)
				{
					TArray<FString> PropertyNames;
					PropertyTrackSettings.PropertyPath.ParseIntoArray(PropertyNames, TEXT("."));
					ExcludePropertyTracksMap.FindOrAdd(PropertyOwner).Append(PropertyNames);
				}
			}
		}

		// add tracks by property
		for (const FLevelSequencePropertyTrackSettings& PropertyTrackSettings : TrackSettings.DefaultPropertyTracks)
		{
			TSharedRef<FPropertyPath> PropertyPath = FPropertyPath::CreateEmpty();
			UObject* PropertyOwner = &Actor;

			// determine object hierarchy
			TArray<FString> ComponentNames;
			PropertyTrackSettings.ComponentPath.ParseIntoArray(ComponentNames, TEXT("."));

			for (const FString& ComponentName : ComponentNames)
			{
				PropertyOwner = FindObjectFast<UObject>(PropertyOwner, *ComponentName);

				if (PropertyOwner == nullptr)
				{
					return;
				}
			}

			UStruct* PropertyOwnerClass = PropertyOwner->GetClass();

			// determine property path
			TArray<FString> PropertyNames;
			PropertyTrackSettings.PropertyPath.ParseIntoArray(PropertyNames, TEXT("."));

			bool bReplaceWithTransformTrack = false;
			for (const FString& PropertyName : PropertyNames)
			{
				// skip past excluded properties
				if (ExcludePropertyTracksMap.Contains(PropertyOwner) && ExcludePropertyTracksMap[PropertyOwner].Contains(PropertyName))
				{
					PropertyPath = FPropertyPath::CreateEmpty();
					break;
				}

				FProperty* Property = PropertyOwnerClass->FindPropertyByName(*PropertyName);

				if (Property != nullptr)
				{
					PropertyPath->AddProperty(FPropertyInfo(Property));

					// Transform tracks are a special case and must be handled separately.
					if (PropertyOwner->IsA(USceneComponent::StaticClass()) &&
						(PropertyName == TEXT("RelativeLocation") || PropertyName == TEXT("RelativeRotation") || PropertyName == TEXT("RelativeScale3D")))
					{
						bReplaceWithTransformTrack = true;
						break;
					}
				}

				FStructProperty* StructProperty = CastField<FStructProperty>(Property);

				if (StructProperty != nullptr)
				{
					PropertyOwnerClass = StructProperty->Struct;
					continue;
				}

				FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);

				if (ObjectProperty != nullptr)
				{
					PropertyOwnerClass = ObjectProperty->PropertyClass;
					continue;
				}

				break;
			}

			const bool bCreateHandleIfMissing = false;
			FGuid ComponentBinding = InSequencer->GetHandleToObject(PropertyOwner, bCreateHandleIfMissing);

			if (bReplaceWithTransformTrack)
			{
				UClass* TrackClass = UMovieScene3DTransformTrack::StaticClass();
				UMovieSceneTrack* NewTrack = MovieScene->FindTrack(TrackClass, ComponentBinding);
				if (!NewTrack)
				{
					NewTrack = MovieScene->AddTrack(TrackClass, ComponentBinding);
					CreateDefaultTrackSection(NewTrack, PropertyOwner);
				}
				continue;
			}

			bool bFoundPropertyTrack = false;
			if (ComponentBinding.IsValid())
			{
				TArray<UMovieSceneTrack*> Tracks = MovieScene->FindTracks(UMovieScenePropertyTrack::StaticClass(), ComponentBinding, NAME_None);
				for (UMovieSceneTrack* Track : Tracks)
				{
					UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(Track);
					if (PropertyTrack)
					{
						if (PropertyTrack->GetPropertyPath() == PropertyTrackSettings.PropertyPath)
						{
							bFoundPropertyTrack = true;
							break;
						}
					}
				}
			}

			if (bFoundPropertyTrack)
			{
				continue;
			}

			if (!InSequencer->CanKeyProperty(FCanKeyPropertyParams(PropertyOwner->GetClass(), *PropertyPath)))
			{
				continue;
			}

			// key property
			FKeyPropertyParams KeyPropertyParams(TArrayBuilder<UObject*>().Add(PropertyOwner), *PropertyPath, ESequencerKeyMode::ManualKey);

			InSequencer->KeyProperty(KeyPropertyParams);
		}
	}

	// callback to set up default tracks via code
	ILevelSequenceModule& LevelSequenceModule = FModuleManager::LoadModuleChecked<ILevelSequenceModule>("LevelSequence");
	LevelSequenceModule.OnNewActorTrackAdded().Broadcast(Actor, Binding, InSequencer);
}

TArray<FMovieSceneBindingProxy> ULevelSequenceEditorSubsystem::AddActors(const TArray<AActor*>& InActors)
{
	TArray<FMovieSceneBindingProxy> BindingProxies;

	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return BindingProxies;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return BindingProxies;
	}

	TArray<TWeakObjectPtr<AActor> > Actors;
	for (AActor* Actor : InActors)
	{
		Actors.Add(Actor);
	}

	TArray<FGuid> Guids = FSequencerUtilities::AddActors(Sequencer.ToSharedRef(), Actors);
	
	for (const FGuid& Guid : Guids)
	{
		BindingProxies.Add(FMovieSceneBindingProxy(Guid, Sequence));
	}

	return BindingProxies;
}

FMovieSceneBindingProxy ULevelSequenceEditorSubsystem::AddSpawnableFromInstance(UMovieSceneSequence* Sequence, UObject* ObjectToSpawn)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return FMovieSceneBindingProxy();
	}

	UMovieSceneSequence* FocusedSequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!FocusedSequence)
	{
		return FMovieSceneBindingProxy();
	}

	if (FocusedSequence != Sequence)
	{
		UE_LOGF(LogLevelSequenceEditor, Error, "AddSpawnableFromInstance requires that the requested sequence %ls be open in the editor", *GetNameSafe(Sequence));
		return FMovieSceneBindingProxy();
	}

	if (!ObjectToSpawn)
	{
		UE_LOGF(LogLevelSequenceEditor, Error, "AddSpawnableFromInstance requires a valid ObjectToSpawn");
		return FMovieSceneBindingProxy();
	}

	UE::Sequencer::FCreateBindingParams Params;
	Params.BindingNameOverride = ObjectToSpawn->GetName();
	Params.bSpawnable = true;

	FGuid Guid = FSequencerUtilities::CreateBinding(Sequencer.ToSharedRef(), *ObjectToSpawn, Params);
	FMovieSceneBindingProxy BindingProxy(Guid, Sequence);
	return BindingProxy;
}

FMovieSceneBindingProxy ULevelSequenceEditorSubsystem::AddSpawnableFromClass(UMovieSceneSequence* Sequence, UClass* ClassToSpawn)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return FMovieSceneBindingProxy();
	}

	UMovieSceneSequence* FocusedSequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!FocusedSequence)
	{
		return FMovieSceneBindingProxy();
	}

	if (FocusedSequence != Sequence)
	{
		UE_LOGF(LogLevelSequenceEditor, Error, "AddSpawnableFromClass requires that the requested sequence %ls be open in the editor", *GetNameSafe(Sequence));
		return FMovieSceneBindingProxy();
	}

	if (!ClassToSpawn)
	{
		UE_LOGF(LogLevelSequenceEditor, Error, "AddSpawnableFromClass requires a valid ClassToSpawn");
		return FMovieSceneBindingProxy();
	}

	UE::Sequencer::FCreateBindingParams Params;
	Params.BindingNameOverride = ClassToSpawn->GetName();
	Params.bSpawnable = true;

	FGuid Guid = FSequencerUtilities::CreateBinding(Sequencer.ToSharedRef(), *ClassToSpawn, Params);
	FMovieSceneBindingProxy BindingProxy(Guid, Sequence);
	return BindingProxy;
}

FMovieSceneBindingProxy ULevelSequenceEditorSubsystem::CreateCamera(bool bSpawnable, ACineCameraActor*& OutActor)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return FMovieSceneBindingProxy();
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return FMovieSceneBindingProxy();
	}

	FGuid Guid = FSequencerUtilities::CreateCamera(Sequencer.ToSharedRef(), bSpawnable, OutActor);

	return FMovieSceneBindingProxy(Guid, Sequence);
}

TArray<FMovieSceneBindingProxy> ULevelSequenceEditorSubsystem::ConvertToSpawnable(const FMovieSceneBindingProxy& ObjectBinding)
{
	TArray<FMovieSceneBindingProxy> SpawnableProxies;

	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return SpawnableProxies;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return SpawnableProxies;
	}

	FMovieScenePossessable* NewPossessable = nullptr;

	if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
	{
		int32 NumBindings = BindingReferences->GetReferences(ObjectBinding.BindingID).Num();
		for (int32 BindingIndex = 0; BindingIndex < NumBindings; BindingIndex++)
		{
			NewPossessable = FSequencerUtilities::ConvertToCustomBinding(Sequencer.ToSharedRef(), ObjectBinding.BindingID, UMovieSceneSpawnableActorBinding::StaticClass(), BindingIndex);
		}
	}

	if (NewPossessable)
	{
		SpawnableProxies.Add(FMovieSceneBindingProxy(NewPossessable->GetGuid(), Sequence));
	}

	return SpawnableProxies;
}

FMovieSceneBindingProxy ULevelSequenceEditorSubsystem::ConvertToPossessable(const FMovieSceneBindingProxy& ObjectBinding)
{
	FMovieSceneBindingProxy PossessableProxy;

	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return PossessableProxy;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return PossessableProxy;
	}

	FMovieScenePossessable* NewPossessable = nullptr;

	if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
	{
		int32 NumBindings = BindingReferences->GetReferences(ObjectBinding.BindingID).Num();
		for (int32 BindingIndex = 0; BindingIndex < NumBindings; BindingIndex++)
		{
			NewPossessable = FSequencerUtilities::ConvertToPossessable(Sequencer.ToSharedRef(), ObjectBinding.BindingID, BindingIndex);
		}
	}

	if (NewPossessable)
	{
		PossessableProxy = FMovieSceneBindingProxy(NewPossessable->GetGuid(), Sequence);
	}

	return PossessableProxy;
}


FMovieSceneBindingProxy ULevelSequenceEditorSubsystem::ConvertToCustomBinding(const FMovieSceneBindingProxy& ObjectBinding, TSubclassOf<UMovieSceneCustomBinding> BindingType)
{
	FMovieSceneBindingProxy PossessableProxy;

	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return PossessableProxy;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return PossessableProxy;
	}

	FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences();
	if (!BindingReferences)
	{
		return PossessableProxy;
	}

	FMovieScenePossessable* NewPossessable = nullptr;

	if (FSequencerUtilities::CanConvertToCustomBinding(Sequencer.ToSharedRef(), ObjectBinding.BindingID, BindingType))
	{
		int32 NumBindings = BindingReferences->GetReferences(ObjectBinding.BindingID).Num();
		for (int32 BindingIndex = 0; BindingIndex < NumBindings; BindingIndex++)
		{
			NewPossessable = FSequencerUtilities::ConvertToCustomBinding(Sequencer.ToSharedRef(), ObjectBinding.BindingID, BindingType, BindingIndex);
		}

		if (NewPossessable)
		{
			PossessableProxy = FMovieSceneBindingProxy(NewPossessable->GetGuid(), Sequence);
		}
	}

	return PossessableProxy;
}

TArray<UMovieSceneCustomBinding*> ULevelSequenceEditorSubsystem::GetCustomBindingObjects(const FMovieSceneBindingProxy& ObjectBinding)
{
	TArray<UMovieSceneCustomBinding*> CustomBindings;
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return CustomBindings;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return CustomBindings;
	}

	if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
	{
		for (const FMovieSceneBindingReference& BindingReference : BindingReferences->GetReferences(ObjectBinding.BindingID))
		{
			if (BindingReference.CustomBinding)
			{
				CustomBindings.Add(BindingReference.CustomBinding);
			}
		}
	}

	return CustomBindings;
}

TArray<FMovieSceneBindingProxy> ULevelSequenceEditorSubsystem::GetCustomBindingsOfType(TSubclassOf<UMovieSceneCustomBinding> CustomBindingType)
{
	TArray<FMovieSceneBindingProxy> Bindings;
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return Bindings;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return Bindings;
	}

	if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
	{
		for (const FMovieSceneBindingReference& BindingReference : BindingReferences->GetAllReferences())
		{
			if (BindingReference.CustomBinding && BindingReference.CustomBinding->IsA(CustomBindingType))
			{
				Bindings.AddUnique(FMovieSceneBindingProxy(BindingReference.ID, Sequence));
			}
		}
	}

	return Bindings;
}

TSubclassOf<UMovieSceneCustomBinding> ULevelSequenceEditorSubsystem::GetCustomBindingType(const FMovieSceneBindingProxy& ObjectBinding)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return nullptr;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return nullptr;
	}

	if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
	{
		if (UMovieSceneCustomBinding* CustomBinding = BindingReferences->GetCustomBinding(ObjectBinding.BindingID, 0))
		{
			return CustomBinding->GetClass();
		}
	}

	return nullptr;
}

bool ULevelSequenceEditorSubsystem::ChangeActorTemplateClass(const FMovieSceneBindingProxy& ObjectBinding, TSubclassOf<AActor> ActorClass)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return false;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return false;
	}

	bool bSuccess = false;

	TArray<FSequencerChangeBindingInfo> Bindings;
	Bindings.Add(FSequencerChangeBindingInfo(ObjectBinding.BindingID, 0));

	FSequencerUtilities::HandleTemplateActorClassPicked(ActorClass, Sequencer.ToSharedRef(), Bindings, [&bSuccess]() {bSuccess = true; });

	return bSuccess;
}

void ULevelSequenceEditorSubsystem::SaveDefaultSpawnableState(const FMovieSceneBindingProxy& ObjectBinding)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}

	Sequencer->GetSpawnRegister().SaveDefaultSpawnableState(ObjectBinding.BindingID, 0, Sequencer->GetFocusedTemplateID(), Sequencer->GetSharedPlaybackState());
}

void ULevelSequenceEditorSubsystem::CopyFolders(const TArray<UMovieSceneFolder*>& Folders, FString& ExportedText)
{
	FString DummyText;
	CopyFolders(Folders, ExportedText, DummyText, DummyText);
}

void ULevelSequenceEditorSubsystem::CopyFolders(const TArray<UMovieSceneFolder*>&Folders, FString &FoldersExportedText, FString &ObjectsExportedText, FString &TracksExportedText)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	FSequencerUtilities::CopyFolders(Sequencer.ToSharedRef(), Folders, FoldersExportedText, ObjectsExportedText, TracksExportedText);

	FString ExportedText;
	ExportedText += ObjectsExportedText;
	ExportedText += TracksExportedText;
	ExportedText += FoldersExportedText;

	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

bool ULevelSequenceEditorSubsystem::PasteFolders(const FString& InTextToImport, FMovieScenePasteFoldersParams PasteFoldersParams, TArray<UMovieSceneFolder*>& OutFolders)
{
	FString TextToImport = InTextToImport;
	if (TextToImport.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardPaste(TextToImport);
	}

	TArray<FNotificationInfo> PasteErrors;
	if (!FSequencerUtilities::PasteFolders(TextToImport, PasteFoldersParams, OutFolders, PasteErrors))
	{
		for (FNotificationInfo PasteError : PasteErrors)
		{
			UE_LOGF(LogLevelSequenceEditor, Error, "%ls", *PasteError.Text.Get().ToString());
		}
		return false;
	}

	return true;
}

void ULevelSequenceEditorSubsystem::CopySections(const TArray<UMovieSceneSection*>& Sections, FString& ExportedText)
{
	FSequencerUtilities::CopySections(Sections, ExportedText);

	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

bool ULevelSequenceEditorSubsystem::PasteSections(const FString& InTextToImport, FMovieScenePasteSectionsParams PasteSectionsParams, TArray<UMovieSceneSection*>& OutSections)
{
	FString TextToImport = InTextToImport;
	if (TextToImport.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardPaste(TextToImport);
	}

	TArray<FNotificationInfo> PasteErrors;
	if (!FSequencerUtilities::PasteSections(TextToImport, PasteSectionsParams, OutSections, PasteErrors))
	{
		for (FNotificationInfo PasteError : PasteErrors)
		{
			UE_LOGF(LogLevelSequenceEditor, Error, "%ls", *PasteError.Text.Get().ToString());
		}
		return false;
	}

	return true;
}

void ULevelSequenceEditorSubsystem::CopyTracks(const TArray<UMovieSceneTrack*>& Tracks, FString& ExportedText)
{
	TArray<UMovieSceneFolder*> Folders;
	FSequencerUtilities::CopyTracks(Tracks, Folders, ExportedText);
	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

bool ULevelSequenceEditorSubsystem::PasteTracks(const FString& InTextToImport, FMovieScenePasteTracksParams PasteTracksParams, TArray<UMovieSceneTrack*>& OutTracks)
{
	FString TextToImport = InTextToImport;
	if (TextToImport.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardPaste(TextToImport);
	}

	TArray<FNotificationInfo> PasteErrors;
	if (!FSequencerUtilities::PasteTracks(TextToImport, PasteTracksParams, OutTracks, PasteErrors))
	{
		for (FNotificationInfo PasteError : PasteErrors)
		{
			UE_LOGF(LogLevelSequenceEditor, Error, "%ls", *PasteError.Text.Get().ToString());
		}
		return false;
	}

	return true;
}

void ULevelSequenceEditorSubsystem::CopyBindings(const TArray<FMovieSceneBindingProxy>& Bindings, FString& ExportedText)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	TArray<UMovieSceneFolder*> Folders;
	FSequencerUtilities::CopyBindings(Sequencer.ToSharedRef(), Bindings, Folders, ExportedText);
	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

bool ULevelSequenceEditorSubsystem::PasteBindings(const FString& InTextToImport, FMovieScenePasteBindingsParams PasteBindingsParams, TArray<FMovieSceneBindingProxy>& OutObjectBindings)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return false;
	}

	FString TextToImport = InTextToImport;
	if (TextToImport.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardPaste(TextToImport);
	}

	TArray<FNotificationInfo> PasteErrors;
	if (!FSequencerUtilities::PasteBindings(TextToImport, Sequencer.ToSharedRef(), PasteBindingsParams, OutObjectBindings, PasteErrors))
	{
		for (FNotificationInfo PasteError : PasteErrors)
		{
			UE_LOGF(LogLevelSequenceEditor, Error, "%ls", *PasteError.Text.Get().ToString());
		}
		return false;
	}

	return true;
}

void ULevelSequenceEditorSubsystem::SnapSectionsToTimelineUsingSourceTimecode(const TArray<UMovieSceneSection*>& Sections)
{
	if (TSharedPtr<ISequencer> Sequencer = GetActiveSequencer())
	{
		FSequencerUtilities::SnapSectionsToTimelineUsingSourceTimecode(Sequencer.ToSharedRef(), Sections);
	}
}

void ULevelSequenceEditorSubsystem::SyncSectionsUsingSourceTimecode(const TArray<UMovieSceneSection*>& Sections)
{
	if (TSharedPtr<ISequencer> Sequencer = GetActiveSequencer())
	{
		FSequencerUtilities::SyncSectionsUsingSourceTimecode(Sequencer.ToSharedRef(), Sections);
	}
}

bool ULevelSequenceEditorSubsystem::BakeTransformWithSettings(const TArray<FMovieSceneBindingProxy>& ObjectBindings, const FBakingAnimationKeySettings& InSettings, const FMovieSceneScriptingParams& Params)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return false;
	}

	FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
	FFrameRate DisplayRate = Sequencer->GetFocusedDisplayRate();

	FBakingAnimationKeySettings Settings = InSettings;

	if (Params.TimeUnit == EMovieSceneTimeUnit::DisplayRate)
	{
		Settings.StartFrame = ConvertFrameTime(Settings.StartFrame, DisplayRate, TickResolution).GetFrame();
		Settings.EndFrame = ConvertFrameTime(Settings.EndFrame, DisplayRate, TickResolution).GetFrame();
	}

	return FSequencerUtilities::BakeTransform(Sequencer.ToSharedRef(), ObjectBindings, Settings);
}

void ULevelSequenceEditorSubsystem::FixActorReferences()
{
	if (TSharedPtr<ISequencer> Sequencer = GetActiveSequencer())
	{
		FSequencerUtilities::FixActorReferences(Sequencer.ToSharedRef());
	}
}

void ULevelSequenceEditorSubsystem::AddActorsToBinding(const TArray<AActor*>& Actors, const FMovieSceneBindingProxy& ObjectBinding)
{
	if (TSharedPtr<ISequencer> Sequencer = GetActiveSequencer())
	{
		FSequencerUtilities::AddActorsToBinding(Sequencer.ToSharedRef(), Actors, ObjectBinding);
	}
}

void ULevelSequenceEditorSubsystem::ReplaceBindingWithActors(const TArray<AActor*>& Actors, const FMovieSceneBindingProxy& ObjectBinding)
{
	if (TSharedPtr<ISequencer> Sequencer = GetActiveSequencer())
	{
		FSequencerUtilities::ReplaceBindingWithActors(Sequencer.ToSharedRef(), Actors, ObjectBinding);
	}
}

void ULevelSequenceEditorSubsystem::RemoveActorsFromBinding(const TArray<AActor*>& Actors, const FMovieSceneBindingProxy& ObjectBinding)
{
	if (TSharedPtr<ISequencer> Sequencer = GetActiveSequencer())
	{
		FSequencerUtilities::RemoveActorsFromBinding(Sequencer.ToSharedRef(), Actors, ObjectBinding);
	}
}

void ULevelSequenceEditorSubsystem::RemoveAllBindings(const FMovieSceneBindingProxy& ObjectBinding)
{
	if (TSharedPtr<ISequencer> Sequencer = GetActiveSequencer())
	{
		FSequencerUtilities::RemoveAllBindings(Sequencer.ToSharedRef(), ObjectBinding);
	}
}

void ULevelSequenceEditorSubsystem::RemoveInvalidBindings(const FMovieSceneBindingProxy& ObjectBinding)
{
	if (TSharedPtr<ISequencer> Sequencer = GetActiveSequencer())
	{
		FSequencerUtilities::RemoveInvalidBindings(Sequencer.ToSharedRef(), ObjectBinding);
	}
}

void ULevelSequenceEditorSubsystem::AddBindingPropertiesMenu(FMenuBuilder& MenuBuilder)
{
	if (TSharedPtr<ISequencer> Sequencer = GetActiveSequencer())
	{
		TSharedRef<SWidget> DetailsView = BuildBindingPropertiesDetailsWidget(Sequencer.ToSharedRef());
		MenuBuilder.AddMenuSeparator();
		MenuBuilder.AddWidget(DetailsView, FText::GetEmpty(), true);
	}
}

void ULevelSequenceEditorSubsystem::AddBindingPropertiesSidebar(FMenuBuilder& MenuBuilder)
{
	AddBindingPropertiesMenu(MenuBuilder);
}

void ULevelSequenceEditorSubsystem::AddTrackRowMetadataMenu(FMenuBuilder& MenuBuilder)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}


	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}

	TArray<TPair<UMovieSceneTrack*, int32>> SelectedTrackRows;

	Sequencer->GetSelectedTrackRows(SelectedTrackRows);

	if (SelectedTrackRows.IsEmpty())
	{
		return;
	}

	NotifyHook = FBindingPropertiesNotifyHook(Sequence);

	// Set up a details panel for the list of selected track row metadata
	FDetailsViewArgs DetailsViewArgs;
	{
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.bCustomFilterAreaLocation = false;
		DetailsViewArgs.bCustomNameAreaLocation = false;
		DetailsViewArgs.bHideSelectionTip = true;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.bSearchInitialKeyFocus = true;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.bShowOptions = false;
		DetailsViewArgs.bShowModifiedPropertiesOption = false;
		DetailsViewArgs.bShowScrollBar = false;
		DetailsViewArgs.bAllowMultipleTopLevelObjects = false;
		DetailsViewArgs.NotifyHook = &NotifyHook;
	}

	TSharedRef<IDetailsView> DetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);

	AddTrackRowMetadataCustomizations(DetailsView, Sequencer, Sequence);

	RefreshTrackRowMetadataDetails(&DetailsView.Get());
	DetailsView->OnFinishedChangingProperties().AddUObject(this, &ULevelSequenceEditorSubsystem::OnFinishedChangingTrackRowMetadata, DetailsView);

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnMenuBeingDestroyed().AddUObject(this, &ULevelSequenceEditorSubsystem::OnTrackRowMetadataMenuBeingDestroyed, DetailsView);
	}
	MenuBuilder.AddWidget(DetailsView, FText::GetEmpty(), true);
}

void ULevelSequenceEditorSubsystem::FBindingPropertiesNotifyHook::NotifyPreChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange != nullptr)
	{
		GEditor->BeginTransaction(FText::Format(LOCTEXT("EditProperty", "Edit {0}"), PropertyAboutToChange->GetDisplayNameText()));

		ObjectToModify->Modify();
	}
}

void ULevelSequenceEditorSubsystem::FBindingPropertiesNotifyHook::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	GEditor->EndTransaction();
}

TSharedRef<SWidget> ULevelSequenceEditorSubsystem::BuildBindingPropertiesDetailsWidget(TSharedRef<ISequencer> Sequencer)
{
	TArray<FGuid> ObjectBindings;
	Sequencer->GetSelectedObjects(ObjectBindings);
	if (ObjectBindings.IsEmpty())
	{
		return SNullWidget::NullWidget;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return SNullWidget::NullWidget;
	}

	FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences();
	if (!BindingReferences)
	{
		return SNullWidget::NullWidget;
	}

	// Set up a details panel for the list of locators
	FDetailsViewArgs DetailsViewArgs;
	{
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.bCustomFilterAreaLocation = true;
		DetailsViewArgs.bCustomNameAreaLocation = true;
		DetailsViewArgs.bHideSelectionTip = true;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.bSearchInitialKeyFocus = true;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.bShowOptions = false;
		DetailsViewArgs.bShowModifiedPropertiesOption = false;
		DetailsViewArgs.bShowScrollBar = false;
		DetailsViewArgs.bAllowMultipleTopLevelObjects = false;

		NotifyHook = FBindingPropertiesNotifyHook(Sequence);
		DetailsViewArgs.NotifyHook = &NotifyHook;

		DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
	}

	TSharedRef<IDetailsView> DetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);

	AddBindingDetailCustomizations(DetailsView, Sequencer, ObjectBindings[0]);

	RefreshBindingDetails(Sequencer, &DetailsView.Get(), ObjectBindings[0]);
	DetailsView->OnFinishedChangingProperties().AddUObject(this, &ULevelSequenceEditorSubsystem::OnFinishedChangingLocators, Sequencer.ToWeakPtr(), DetailsView, ObjectBindings[0]);

	return DetailsView;
}

void ULevelSequenceEditorSubsystem::OnFinishedChangingLocators(const FPropertyChangedEvent& PropertyChangedEvent, TWeakPtr<ISequencer> WeakSequencer, TSharedRef<IDetailsView> DetailsView, FGuid ObjectBindingID)
{
	const FScopedTransaction Transaction(LOCTEXT("ChangeBindingProperties", "Change Binding Properties"));

	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	if (!Sequencer)
	{
		return;
	}

	UMovieSceneBindingPropertyInfoList* BindingPropertyInfoList = GetBindingPropertyInfoList(Sequencer.ToSharedRef());
	if (!BindingPropertyInfoList)
	{
		return;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return;
	}
	if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
	{
		// A bit hacky, but saves a complicated detail customization. If the change we've just made is to add a new entry, ensure the new entry is initialized
		// to the same binding type as previous entries.
		TArrayView<const FMovieSceneBindingReference> PreviousReferences = BindingReferences->GetReferences(ObjectBindingID);
		if (PreviousReferences.Num() > 0 && PreviousReferences.Num() == BindingPropertyInfoList->Bindings.Num() - 1)
		{
			if (UMovieSceneCustomBinding* PreviousCustomBinding = BindingPropertyInfoList->Bindings[BindingPropertyInfoList->Bindings.Num() - 2].CustomBinding)
			{
				BindingPropertyInfoList->Bindings[BindingPropertyInfoList->Bindings.Num() - 1].CustomBinding = NewObject<UMovieSceneCustomBinding>(MovieScene, PreviousCustomBinding->GetClass());
			}
		}

		MovieScene->Modify();
		Sequence->Modify();
		// Clear the previous binding
		BindingReferences->RemoveBinding(ObjectBindingID);

		// Add the new updated bindings
		for (FMovieSceneBindingPropertyInfo& LocatorInfo : BindingPropertyInfoList->Bindings)
		{
			UMovieSceneCustomBinding* CopiedBinding = nullptr;
			if (LocatorInfo.CustomBinding)
			{
				CopiedBinding = Cast<UMovieSceneCustomBinding>(StaticDuplicateObject(LocatorInfo.CustomBinding, MovieScene));
			}
			BindingReferences->AddBinding(ObjectBindingID, MoveTemp(LocatorInfo.Locator), LocatorInfo.ResolveFlags, CopiedBinding);
			if (CopiedBinding)
			{
				CopiedBinding->OnBindingAddedOrChanged(*MovieScene);
			}
		}

		Sequencer->GetEvaluationState()->Invalidate(ObjectBindingID, Sequencer->GetFocusedTemplateID());

		// Update the object class and DisplayName
		TArrayView<TWeakObjectPtr<>> ObjectsInCurrentSequence = Sequencer->FindObjectsInCurrentSequence(ObjectBindingID);
		UClass* ObjectClass = nullptr;

		for (TWeakObjectPtr<> Ptr : ObjectsInCurrentSequence)
		{
			if (UObject* BoundObject = Ptr.Get())
			{
				if (ObjectClass == nullptr)
				{
					ObjectClass = BoundObject->GetClass();
				}
				else
				{
					ObjectClass = UClass::FindCommonBase(BoundObject->GetClass(), ObjectClass);
				}
			}
		}

		// Update label
		if (ObjectsInCurrentSequence.Num() > 0)
		{
			FMovieScenePossessable* Possessable = MovieScene->FindPossessable(ObjectBindingID);
			if (Possessable && ObjectClass != nullptr)
			{
				if (ObjectsInCurrentSequence.Num() > 1)
				{
					FString NewLabel = ObjectClass->GetName() + FString::Printf(TEXT(" (%d)"), ObjectsInCurrentSequence.Num());
					Possessable->SetName(NewLabel);
				}
				else if (AActor* Actor = Cast<AActor>(ObjectsInCurrentSequence[0].Get()))
				{
					Possessable->SetName(Actor->GetActorLabel());
				}
				else if (UActorComponent* ActorComponent = Cast<UActorComponent>(ObjectsInCurrentSequence[0].Get()))
				{
					Possessable->SetName(ActorComponent->GetName());
				}
				else
				{
					Possessable->SetName(ObjectClass->GetName());
				}

				Possessable->SetPossessedObjectClass(ObjectClass);
			}
		}

		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);

		// Destroy any previous spawnables- they'll get recreated on the force evaluate below
		for (int32 BindingIndex = 0; BindingIndex < BindingPropertyInfoList->Bindings.Num(); ++BindingIndex)
		{
			Sequencer->GetSpawnRegister().DestroySpawnedObject(ObjectBindingID, Sequencer->GetFocusedTemplateID(), Sequencer->GetSharedPlaybackState(), BindingIndex);
		}

		// Force evaluate the Sequencer after clearing the cache (which the above will do) so that any newly loaded actors will be loaded as part of the transaction
		Sequencer->ForceEvaluate();

		// Send the OnAddBinding message, which will add a Binding Lifetime Track if necessary
		Sequencer->OnAddBinding(ObjectBindingID, MovieScene);

		// Re-copy the locator info back into the struct details
		BindingPropertyInfoList->Bindings.Empty();
		Algo::Transform(BindingReferences->GetReferences(ObjectBindingID), BindingPropertyInfoList->Bindings, [this](const FMovieSceneBindingReference& Reference)
			{
				UMovieSceneCustomBinding* CopiedBinding = nullptr;
				if (Reference.CustomBinding)
				{
					CopiedBinding = Cast<UMovieSceneCustomBinding>(StaticDuplicateObject(Reference.CustomBinding, this));
				}

				return FMovieSceneBindingPropertyInfo{ Reference.Locator, Reference.ResolveFlags, CopiedBinding };
			});


		// Force the struct details view to refresh
		DetailsView->InvalidateCachedState();
	}
}

void ULevelSequenceEditorSubsystem::OnFinishedChangingTrackRowMetadata(const FPropertyChangedEvent& PropertyChangedEvent, TSharedRef<IDetailsView> DetailsView)
{
	const FScopedTransaction Transaction(LOCTEXT("ChangeTrackRowMetadata", "Change Track Row Metadata"));

	if (TrackRowMetadataHelperList.IsEmpty())
	{
		return;
	}

	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}
	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return;
	}

	MovieScene->Modify();
	Sequence->Modify();

	TArray<TPair<UMovieSceneTrack*, int32>> SelectedTrackRows;

	Sequencer->GetSelectedTrackRows(SelectedTrackRows);

	ensure(SelectedTrackRows.Num() == TrackRowMetadataHelperList.Num());
	
	// Copy over the new metadata, but duplicate any condition trees over to new ownership in the sequence
	for (int32 Index = 0; Index < TrackRowMetadataHelperList.Num(); ++Index)
	{
		UMovieSceneTrack* Track = SelectedTrackRows[Index].Key;
		UMovieSceneTrackRowMetadataHelper* Helper = TrackRowMetadataHelperList[Index];
		if (Track && Helper)
		{
			FMovieSceneTrackRowMetadata& Metadata = Track->FindOrAddTrackRowMetadata(SelectedTrackRows[Index].Value);
			Metadata = Helper->TrackRowMetadata;
			if (Helper->TrackRowMetadata.ConditionContainer.Condition)
			{
				Metadata.ConditionContainer.Condition = Cast<UMovieSceneCondition>(StaticDuplicateObject(Helper->TrackRowMetadata.ConditionContainer.Condition, MovieScene));
			}
		}
	}

	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);

	// Re-copy the metadata info back into the struct details
	RefreshTrackRowMetadataDetails(&DetailsView.Get());

	// Force the struct details view to refresh
	DetailsView->InvalidateCachedState();
}

void ULevelSequenceEditorSubsystem::GetRebindComponentNames(TArray<FName>& OutComponentNames)
{
	OutComponentNames.Empty();

	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return;
	}

	TArray<FGuid> ObjectBindings;
	Sequencer->GetSelectedObjects(ObjectBindings);
	if (ObjectBindings.Num() == 0)
	{
		return;
	}

	FGuid ComponentGuid = ObjectBindings[0];

	FMovieScenePossessable* ComponentPossessable = MovieScene->FindPossessable(ComponentGuid);

	FGuid ActorParentGuid = ComponentPossessable ? ComponentPossessable->GetParent() : FGuid();

	TArrayView<TWeakObjectPtr<>> ObjectsInCurrentSequence = Sequencer->FindObjectsInCurrentSequence(ActorParentGuid);

	const AActor* Actor = nullptr;
	for (TWeakObjectPtr<> Ptr : ObjectsInCurrentSequence)
	{
		Actor = Cast<AActor>(Ptr.Get());
		if (Actor)
		{
			break;
		}
	}

	if (!Actor)
	{
		return;
	}
		
	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
	const TSharedPtr<IClassViewerFilter>& GlobalClassFilter = ClassViewerModule.GetGlobalClassViewerFilter();
	TSharedRef<FClassViewerFilterFuncs> ClassFilterFuncs = ClassViewerModule.CreateFilterFuncs();
	FClassViewerInitializationOptions ClassViewerOptions = {};

	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (Component && ComponentPossessable && Component->GetName() != ComponentPossessable->GetName())
		{
			bool bValidComponent = !Component->IsVisualizationComponent();

			if (GlobalClassFilter.IsValid())
			{
				// Hack - forcibly allow USkeletalMeshComponentBudgeted until FORT-527888
				static const FName SkeletalMeshComponentBudgetedClassName(TEXT("SkeletalMeshComponentBudgeted"));
				if (Component->GetClass()->GetName() == SkeletalMeshComponentBudgetedClassName)
				{
					bValidComponent = true;
				}
				else
				{
					bValidComponent = GlobalClassFilter->IsClassAllowed(ClassViewerOptions, Component->GetClass(), ClassFilterFuncs);
				}
			}

			if (bValidComponent)
			{
				OutComponentNames.Add(Component->GetFName());
			}
		}
	}
	OutComponentNames.Sort(FNameFastLess());
}

void ULevelSequenceEditorSubsystem::RebindComponentMenu(FMenuBuilder& MenuBuilder)
{
	TArray<FName> ComponentNames;
	GetRebindComponentNames(ComponentNames);

	for (const FName& ComponentName : ComponentNames)
	{
		FText RebindComponentLabel = FText::FromName(ComponentName);
		MenuBuilder.AddMenuEntry(
			RebindComponentLabel, 
			FText(), 
			FSlateIcon(), 
			FUIAction(FExecuteAction::CreateLambda([this, ComponentName]() { RebindComponentInternal(ComponentName); } ) ) );
	}
}

void ULevelSequenceEditorSubsystem::RebindComponentInternal(const FName& ComponentName)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	TArray<FGuid> ObjectBindings;
	Sequencer->GetSelectedObjects(ObjectBindings);
	if (ObjectBindings.Num() == 0)
	{
		return;
	}

	TArray<FMovieSceneBindingProxy> BindingProxies;
	for (const FGuid& ObjectBinding : ObjectBindings)
	{
		FMovieSceneBindingProxy BindingProxy(ObjectBinding, Sequencer->GetFocusedMovieSceneSequence());
		BindingProxies.Add(BindingProxy);
	}

	RebindComponent(BindingProxies, ComponentName);
}

void ULevelSequenceEditorSubsystem::RebindComponent(const TArray<FMovieSceneBindingProxy>& PossessableBindings, const FName& ComponentName)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return;
	}

	FScopedTransaction RebindComponent(LOCTEXT("RebindComponent", "Rebind Component"));

	Sequence->Modify();
	MovieScene->Modify();

	bool bAnythingChanged = false;
	for (const FMovieSceneBindingProxy& PossessableBinding : PossessableBindings)
	{
		FMovieScenePossessable* ComponentPossessable = MovieScene->FindPossessable(PossessableBinding.BindingID);

		FGuid ActorParentGuid = ComponentPossessable ? ComponentPossessable->GetParent() : FGuid();

		TArrayView<TWeakObjectPtr<>> ObjectsInCurrentSequence = Sequencer->FindObjectsInCurrentSequence(ActorParentGuid);

		for (TWeakObjectPtr<> Ptr : ObjectsInCurrentSequence)
		{
			if (const AActor* Actor = Cast<AActor>(Ptr.Get()))
			{
				for (UActorComponent* Component : Actor->GetComponents())
				{
					if (Component->GetFName() == ComponentName)
					{
						FGuid ComponentBinding = Sequence->CreatePossessable(Component);
						
						if (PossessableBinding.BindingID.IsValid() && ComponentBinding.IsValid())
						{
							MovieScene->MoveBindingContents(PossessableBinding.BindingID, ComponentBinding);

							bAnythingChanged = true;
						}
					}
				}
			}
		}
	}

	if (bAnythingChanged)
	{
		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	}
}

UMovieSceneBindingPropertyInfoList* ULevelSequenceEditorSubsystem::GetBindingPropertyInfoList(TSharedRef<ISequencer> Sequencer)
{
	TObjectPtr<UMovieSceneBindingPropertyInfoList>* BindingPropertyInfoListPtr = BindingPropertyInfoLists.FindByPredicate([Sequencer](const TObjectPtr<UMovieSceneBindingPropertyInfoList>& List) { return List->Sequencer == Sequencer; });
	if (BindingPropertyInfoListPtr)
	{
		return *BindingPropertyInfoListPtr;
	}

	return nullptr;
}

void ULevelSequenceEditorSubsystem::RefreshBindingDetails(IDetailsView* DetailsView, FGuid ObjectBindingID)
{
	if (TSharedPtr<ISequencer> Sequencer = GetActiveSequencer())
	{
		RefreshBindingDetails(Sequencer.ToSharedRef(), DetailsView, ObjectBindingID);
	}
}

void ULevelSequenceEditorSubsystem::RefreshBindingDetails(TSharedRef<ISequencer> Sequencer, IDetailsView* DetailsView, FGuid ObjectBindingID)
{
	if (DetailsView == nullptr)
	{
		return;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}

	FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences();
	if (!BindingReferences)
	{
		return;
	}

	UMovieSceneBindingPropertyInfoList* BindingPropertyInfoList = GetBindingPropertyInfoList(Sequencer);
	if (!BindingPropertyInfoList)
	{
		return;
	}

	BindingPropertyInfoList->Bindings.Empty();

	Algo::Transform(BindingReferences->GetReferences(ObjectBindingID), BindingPropertyInfoList->Bindings, [this](const FMovieSceneBindingReference& Reference)
		{
			UMovieSceneCustomBinding* CopiedBinding = nullptr;
			if (Reference.CustomBinding)
			{
				CopiedBinding = Cast<UMovieSceneCustomBinding>(StaticDuplicateObject(Reference.CustomBinding, this));
			}
			return FMovieSceneBindingPropertyInfo{ Reference.Locator, Reference.ResolveFlags, CopiedBinding };
		});

	DetailsView->SetObject(BindingPropertyInfoList, true);
}

void ULevelSequenceEditorSubsystem::RefreshTrackRowMetadataDetails(IDetailsView* DetailsView)
{
	if (DetailsView == nullptr)
	{
		return;
	}
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}


	TArray<TPair<UMovieSceneTrack*, int32>> SelectedTrackRows;

	Sequencer->GetSelectedTrackRows(SelectedTrackRows);

	TrackRowMetadataHelperList.Empty();

	// Copy over the metadata, but duplicate any condition trees over to new ownership
	for (int32 Index = 0; Index < SelectedTrackRows.Num(); ++Index)
	{
		UMovieSceneTrack* Track = SelectedTrackRows[Index].Key;
		UMovieSceneTrackRowMetadataHelper* Helper = TrackRowMetadataHelperList.Add_GetRef(NewObject<UMovieSceneTrackRowMetadataHelper>(this));
		if (Track && Helper)
		{
			Helper->OwnerTrack = Track;
			if (const FMovieSceneTrackRowMetadata* Metadata = Track->FindTrackRowMetadata(SelectedTrackRows[Index].Value))
			{
				Helper->TrackRowMetadata = *Metadata;
				if (Metadata->ConditionContainer.Condition)
				{
					Helper->TrackRowMetadata.ConditionContainer.Condition = Cast<UMovieSceneCondition>(StaticDuplicateObject(Metadata->ConditionContainer.Condition, this));
				}
			}
		}
	}
	TArray<TWeakObjectPtr<UObject>> WeakHelpers;
	Algo::Transform(TrackRowMetadataHelperList, WeakHelpers, [](TObjectPtr<UMovieSceneTrackRowMetadataHelper> HelperPtr) { return HelperPtr;});
	DetailsView->SetObjects(WeakHelpers, true);
}


void ULevelSequenceEditorSubsystem::AddChangeBindingTypeMenu(FMenuBuilder& MenuBuilder, TSharedRef<ISequencer> Sequencer, const TArray<FSequencerChangeBindingInfo>& BindingsToChange, bool bConvert, TFunction<void()> OnBindingChanged)
{
	using namespace UE::Sequencer;

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;

	if (!MovieScene || BindingsToChange.IsEmpty())
	{
		return;
	}

	const TWeakPtr<ISequencer> WeakSequencer = Sequencer;

	if (BindingsToChange.Num() == 1)
	{
		// This is captured by-value into the submenu lambda to keep it alive while the menu is open
		TSharedPtr<FProxyObjectBindingIDPicker> ProxyPicker = MakeShared<FProxyObjectBindingIDPicker>(Sequencer, BindingsToChange[0].BindingID, [this, WeakSequencer, Sequence, BindingsToChange, OnBindingChanged](FMovieSceneObjectBindingID ID){

			if (TSharedPtr<ISequencer> SequencerPtr = WeakSequencer.Pin())
			{
				auto OnChanged = [Sequence, ID](FGuid BindingID, int32 BindingIndex)
				{
					if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
					{
						BindingReferences->AddOrReplaceBinding(BindingID, FUniversalObjectLocator(), BindingIndex);
					}

					return Sequence->GetMovieScene()->FindPossessable(BindingID);
				};

				this->ChangeBindingTypes(
					SequencerPtr.ToSharedRef(),
					BindingsToChange,
					OnChanged,
					OnBindingChanged
				);
			}
		});

		MenuBuilder.AddSubMenu(
			LOCTEXT("ConvertToProxy", "Proxy Binding"),
			LOCTEXT("ConvertToProxyTooltip", "Convert selected binding(s) to a proxy binding that simply references a binding in another sequence"),
			FNewMenuDelegate::CreateLambda([ProxyPicker](FMenuBuilder& SubMenuBuilder) {
				ProxyPicker->GetPickerMenu(SubMenuBuilder);
			}),
			false,
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Sequencer.ProxyIconOverlay")
		);
	}

	// Can convert to possessable
	if (!bConvert || Algo::AllOf(BindingsToChange, [this, Sequencer, Sequence, MovieScene](const FSequencerChangeBindingInfo& BindingInfo) {
		return FSequencerUtilities::CanConvertToPossessable(Sequencer, BindingInfo.BindingID, BindingInfo.BindingIndex);
		}))
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ConvertToPossessable", "Possessable"),
			bConvert ? LOCTEXT("ConvertToPossessableTooltip", "Convert selected binding(s) to a possessable") : LOCTEXT("ChangeToPossessableTooltip", "Reset selected binding(s) to a new possessable"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, WeakSequencer, Sequence, BindingsToChange, bConvert, OnBindingChanged]()
				{
					const TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
					if (!Sequencer)
					{
						return;
					}

					const TSharedRef<ISequencer> SequencerRef = Sequencer.ToSharedRef();

					ChangeBindingTypes(SequencerRef, BindingsToChange, [SequencerRef, Sequence, bConvert](FGuid BindingID, int32 BindingIndex)
						{
							if (bConvert)
							{
								return FSequencerUtilities::ConvertToPossessable(SequencerRef, BindingID, BindingIndex);
							}
							else if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
							{
								BindingReferences->AddOrReplaceBinding(BindingID, FUniversalObjectLocator(), BindingIndex);
								return Sequence->GetMovieScene()->FindPossessable(BindingID);
							}
							return (FMovieScenePossessable*)nullptr;
						},
						OnBindingChanged);

				})));
	}


	// Sort custom binding types by engine types vs. non-engine (custom user types)
	TArrayView<const TSubclassOf<UMovieSceneCustomBinding>> PrioritySortedCustomBindingTypes = Sequencer->GetSupportedCustomBindingTypes();
	TArray<const TSubclassOf<UMovieSceneCustomBinding>> EngineClasses;
	TArray<const TSubclassOf<UMovieSceneCustomBinding>> UserClasses;
	for (const TSubclassOf<UMovieSceneCustomBinding>& CustomBindingType : PrioritySortedCustomBindingTypes)
	{
		FString PackagePathName = CustomBindingType->GetPackage()->GetPathName();
		if (PackagePathName.StartsWith(TEXT("/Engine")) || PackagePathName.StartsWith(TEXT("/Script")))
		{
			EngineClasses.Add(CustomBindingType);
		}
		else
		{
			UserClasses.Add(CustomBindingType);
		}
	}

	// Show built-in classes first
	for (const TSubclassOf<UMovieSceneCustomBinding>& CustomBindingType : EngineClasses)
	{
		FString BindingTypePath = CustomBindingType->GetPathName();
		FString OtherPath = CustomBindingType->GetPackage()->GetPathName();

		// Can convert to custom bindings
		if (!bConvert || Algo::AllOf(BindingsToChange, [this, &Sequence, &MovieScene, CustomBindingType, Sequencer](const FSequencerChangeBindingInfo& BindingInfo)
			{
				return FSequencerUtilities::CanConvertToCustomBinding(Sequencer, BindingInfo.BindingID, CustomBindingType, BindingInfo.BindingIndex);
			}))
		{
			// Special case director blueprint bindings to show sub-menu for setting endpoints
			if (CustomBindingType == UMovieSceneSpawnableDirectorBlueprintBinding::StaticClass()
				|| CustomBindingType == UMovieSceneReplaceableDirectorBlueprintBinding::StaticClass())
			{
				// Option to use a director blueprint condition and create or quick bind to an endpoint
				MenuBuilder.AddSubMenu(
					FText::Format(LOCTEXT("ConvertToDirectorBlueprintBinding", "{0}..."), CustomBindingType->GetDefaultObject<UMovieSceneCustomBinding>()->GetBindingTypePrettyName()),
					bConvert ? FText::Format(LOCTEXT("ConvertToCustomBindingTooltip", "Convert selected binding to {0}"), CustomBindingType->GetDefaultObject<UMovieSceneCustomBinding>()->GetBindingTypePrettyName())
					: FText::Format(LOCTEXT("ChangeToCustomBindingTooltip", "Reset selected binding to a new {0}"), CustomBindingType->GetDefaultObject<UMovieSceneCustomBinding>()->GetBindingTypePrettyName()),
					FNewMenuDelegate::CreateLambda([this, WeakSequencer, BindingsToChange, bConvert, OnBindingChanged, CustomBindingType](FMenuBuilder& SubMenuBuilder)
					{
						if (TSharedPtr<ISequencer> PinnedSequencer = WeakSequencer.Pin())
						{
							FillDirectorBlueprintBindingSubMenu(SubMenuBuilder, PinnedSequencer.ToSharedRef(), BindingsToChange, bConvert, OnBindingChanged, CustomBindingType);
						}
					}),
					false,
					CustomBindingType->GetDefaultObject<UMovieSceneCustomBinding>()->GetBindingTrackCustomIconOverlay()
				);
			}
			else
			{
				MenuBuilder.AddMenuEntry(
					CustomBindingType->GetDefaultObject<UMovieSceneCustomBinding>()->GetBindingTypePrettyName(),
					bConvert ? FText::Format(LOCTEXT("ConvertToCustomBindingTooltip", "Convert selected binding to {0}"), CustomBindingType->GetDefaultObject<UMovieSceneCustomBinding>()->GetBindingTypePrettyName())
					: FText::Format(LOCTEXT("ChangeToCustomBindingTooltip", "Reset selected binding to a new {0}"), CustomBindingType->GetDefaultObject<UMovieSceneCustomBinding>()->GetBindingTypePrettyName()),
					CustomBindingType->GetDefaultObject<UMovieSceneCustomBinding>()->GetBindingTrackCustomIconOverlay(),
					FUIAction(FExecuteAction::CreateLambda([this, WeakSequencer, CustomBindingType, Sequence, bConvert, BindingsToChange, OnBindingChanged]()
						{
							const TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
							if (!Sequencer)
							{
								return;
							}

							const TSharedRef<ISequencer> SequencerRef = Sequencer.ToSharedRef();
							ChangeBindingTypes(SequencerRef, BindingsToChange, [SequencerRef, Sequence, bConvert, CustomBindingType](FGuid BindingID, int32 BindingIndex)
								{
									if (bConvert)
									{
										return FSequencerUtilities::ConvertToCustomBinding(SequencerRef, BindingID, CustomBindingType, BindingIndex);
									}
									else if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
									{
										BindingReferences->AddOrReplaceBinding(BindingID,
											NewObject<UMovieSceneCustomBinding>(Sequence->GetMovieScene(), CustomBindingType),
											BindingIndex);
										return Sequence->GetMovieScene()->FindPossessable(BindingID);
									}
									return (FMovieScenePossessable*)nullptr;
								},
								OnBindingChanged);
						})));
			}
		}
	}

	MenuBuilder.AddSeparator();

	// Custom classes
	MenuBuilder.AddSubMenu(
		LOCTEXT("CustomBindings", "Custom Bindings..."),
		LOCTEXT("CustomBindingsTooltip", "Choose or create a custom binding type"),
		FNewMenuDelegate::CreateLambda([this, WeakSequencer, BindingsToChange, bConvert, OnBindingChanged, UserClasses](FMenuBuilder& SubMenuBuilder)
		{
			if (TSharedPtr<ISequencer> PinnedSequencer = WeakSequencer.Pin())
			{
				FillBindingClassSubMenu(SubMenuBuilder, PinnedSequencer.ToSharedRef(), BindingsToChange, bConvert, OnBindingChanged, UserClasses);
			}
		}),
		false
	);

}

void ULevelSequenceEditorSubsystem::FillDirectorBlueprintBindingSubMenu(FMenuBuilder& MenuBuilder, TSharedRef<ISequencer> Sequencer, const TArray<FSequencerChangeBindingInfo>& BindingsToChange, bool bConvert, TFunction<void()> OnBindingChanged, const TSubclassOf<UMovieSceneCustomBinding>& CustomBindingType)
{
	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
	const TWeakPtr<ISequencer> WeakSequencer = Sequencer;
	if (Sequence)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreateBindingEndpoint_Text", "Create New Binding Endpoint"),
			LOCTEXT("CreateBindingEndpoint_Tooltip", "Creates a new binding endpoint in this sequence's blueprint."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Sequencer.CreateEventBinding"),
			FUIAction(
				FExecuteAction::CreateLambda([this, WeakSequencer, CustomBindingType, Sequence, bConvert, BindingsToChange, OnBindingChanged]()
					{
						const TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
						if (!Sequencer)
						{
							return;
						}

						const TSharedRef<ISequencer> SequencerRef = Sequencer.ToSharedRef();

						if (Sequence)
						{
							// Change or convert the binding
							ChangeBindingTypes(SequencerRef, BindingsToChange, [SequencerRef, Sequence, bConvert, CustomBindingType](FGuid BindingID, int32 BindingIndex)
								{
									FMovieScenePossessable* NewPossessable = nullptr;
									if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
									{
										if (bConvert)
										{
											NewPossessable = FSequencerUtilities::ConvertToCustomBinding(SequencerRef, BindingID, CustomBindingType, BindingIndex);
										}
										else
										{
											BindingReferences->AddOrReplaceBinding(BindingID,
												NewObject<UMovieSceneCustomBinding>(Sequence->GetMovieScene(), CustomBindingType),
												BindingIndex);
											NewPossessable = Sequence->GetMovieScene()->FindPossessable(BindingID);
										}

										UMovieSceneCustomBinding* NewCustomBinding = BindingReferences->GetCustomBinding(BindingID, BindingIndex);

										TArray<void*> RawData;
										if (UMovieSceneReplaceableDirectorBlueprintBinding* ReplaceableBinding = Cast<UMovieSceneReplaceableDirectorBlueprintBinding>(NewCustomBinding))
										{
											RawData.Add(&ReplaceableBinding->DynamicBinding);
										}
										else if (UMovieSceneSpawnableDirectorBlueprintBinding* SpawnableBinding = Cast<UMovieSceneSpawnableDirectorBlueprintBinding>(NewCustomBinding))
										{
											RawData.Add(&SpawnableBinding->DynamicBinding);
										}

										// Create temporary director blueprint binding customization for use in creating the endpoint
										TSharedRef<FMovieSceneDynamicBindingCustomization> BlueprintBindingCustomization = StaticCastSharedRef<FMovieSceneDynamicBindingCustomization>(FMovieSceneDynamicBindingCustomization::MakeInstance(Sequence->GetMovieScene(), BindingID, BindingIndex));
										BlueprintBindingCustomization->SetRawData(RawData);
										BlueprintBindingCustomization->CreateEndpoint();
									}

									return NewPossessable;
								},
								OnBindingChanged);
						}
					}
				)
			));

		MenuBuilder.AddSubMenu(
			LOCTEXT("CreateQuickBinding_Text", "Quick Bind"),
			LOCTEXT("CreateQuickBinding_Tooltip", "Shows a list of functions in this sequence's blueprint that can be used for bindings."),
			FNewMenuDelegate::CreateLambda([this, WeakSequencer, BindingsToChange, bConvert, OnBindingChanged, CustomBindingType](FMenuBuilder& SubMenuBuilder)
			{
				if (TSharedPtr<ISequencer> PinnedSequencer = WeakSequencer.Pin())
				{
					PopulateQuickBindSubMenu(SubMenuBuilder, PinnedSequencer.ToSharedRef(), BindingsToChange, bConvert, OnBindingChanged, CustomBindingType);
				}
			}),
			false /* bInOpenSubMenuOnClick */,
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Sequencer.CreateQuickBinding"),
			false /* bInShouldWindowAfterMenuSelection */
		);
	}
}

void ULevelSequenceEditorSubsystem::PopulateQuickBindSubMenu(FMenuBuilder& MenuBuilder, TSharedRef<ISequencer> Sequencer, const TArray<FSequencerChangeBindingInfo>& BindingsToChange, bool bConvert, TFunction<void()> OnBindingChanged, const TSubclassOf<UMovieSceneCustomBinding>& CustomBindingType)
{
	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;

	if (!MovieScene || BindingsToChange.IsEmpty())
	{
		return;
	}

	TSharedRef<FMovieSceneDynamicBindingCustomization> BlueprintBindingCustomization = StaticCastSharedRef<FMovieSceneDynamicBindingCustomization>(FMovieSceneDynamicBindingCustomization::MakeInstance(Sequence->GetMovieScene(), BindingsToChange[0].BindingID, BindingsToChange[0].BindingIndex));

	const TWeakPtr<ISequencer> WeakSequencer = Sequencer;

	BlueprintBindingCustomization->PopulateQuickBindSubMenu(MenuBuilder,
		Sequence,
		FOnQuickBindActionSelected::CreateLambda([this, WeakSequencer, BindingsToChange, Sequence, bConvert, OnBindingChanged, CustomBindingType]
		(
			const TArray<TSharedPtr<FEdGraphSchemaAction>>& SelectedAction,
			ESelectInfo::Type InSelectionType,
			UBlueprint* Blueprint,
			FMovieSceneDirectorBlueprintEndpointDefinition EndpointDefinition
			)
			{
				const TSharedPtr<ISequencer> PinnedSequencer = WeakSequencer.Pin();
				if (!PinnedSequencer)
				{
					return;
				}

				if (!SelectedAction.IsEmpty())
				{
					const TSharedRef<ISequencer> SequencerRef = PinnedSequencer.ToSharedRef();
					// Change or convert the binding
					ChangeBindingTypes(SequencerRef, BindingsToChange, [SequencerRef, Sequence, bConvert, OnBindingChanged, CustomBindingType, &SelectedAction, InSelectionType, Blueprint, EndpointDefinition](FGuid BindingID, int32 BindingIndex)
						{
							FMovieScenePossessable* NewPossessable = nullptr;
							if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
							{
								if (bConvert)
								{
									NewPossessable = FSequencerUtilities::ConvertToCustomBinding(SequencerRef, BindingID, CustomBindingType, BindingIndex);
								}
								else
								{
									BindingReferences->AddOrReplaceBinding(BindingID,
										NewObject<UMovieSceneCustomBinding>(Sequence->GetMovieScene(), CustomBindingType),
										BindingIndex);
									NewPossessable = Sequence->GetMovieScene()->FindPossessable(BindingID);
								}

								UMovieSceneCustomBinding* NewCustomBinding = BindingReferences->GetCustomBinding(BindingID, BindingIndex);

								TArray<void*> RawData;
								if (UMovieSceneReplaceableDirectorBlueprintBinding* ReplaceableBinding = Cast<UMovieSceneReplaceableDirectorBlueprintBinding>(NewCustomBinding))
								{
									RawData.Add(&ReplaceableBinding->DynamicBinding);
								}
								else if (UMovieSceneSpawnableDirectorBlueprintBinding* SpawnableBinding = Cast<UMovieSceneSpawnableDirectorBlueprintBinding>(NewCustomBinding))
								{
									RawData.Add(&SpawnableBinding->DynamicBinding);
								}

								// Create temporary director blueprint binding customization for use in creating the endpoint
								TSharedRef<FMovieSceneDynamicBindingCustomization> BlueprintBindingCustomization = StaticCastSharedRef<FMovieSceneDynamicBindingCustomization>(FMovieSceneDynamicBindingCustomization::MakeInstance(Sequence->GetMovieScene(), BindingID, BindingIndex));
								BlueprintBindingCustomization->SetRawData(RawData);
								BlueprintBindingCustomization->HandleQuickBindActionSelected(SelectedAction, InSelectionType, Blueprint, EndpointDefinition);

							}
							return NewPossessable;
						},
						OnBindingChanged);
				}
			}));
}

void ULevelSequenceEditorSubsystem::FillBindingClassSubMenu(FMenuBuilder& MenuBuilder, TSharedRef<ISequencer> Sequencer, const TArray<FSequencerChangeBindingInfo>& BindingsToChange, bool bConvert, TFunction<void()> OnBindingsChanged, const TArray<const TSubclassOf<UMovieSceneCustomBinding>>& UserCustomBindingTypes)
{
	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;

	if (!MovieScene || BindingsToChange.IsEmpty())
	{
		return;
	}

	const TWeakPtr<ISequencer> WeakSequencer = Sequencer;

	// If we allow creating binding classes from this replaceable actor binding...
	if (MovieScene->IsCustomBindingClassAllowed(UMovieSceneReplaceableActorBinding_BPBase::StaticClass()))
	{
		// Create a new custom binding Class
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreateNewCustomBindingClass", "Create new Replaceable Binding Class"),
			LOCTEXT("CreateNewCustomBindingClassTooltip", "Creates a new replaceable binding blueprint class"),
			UMovieSceneReplaceableActorBinding_BPBase::StaticClass()->GetDefaultObject<UMovieSceneReplaceableActorBinding_BPBase>()->GetBindingTrackCustomIconOverlay(),
			FUIAction(FExecuteAction::CreateLambda([this, WeakSequencer, Sequence, bConvert, BindingsToChange, OnBindingsChanged]()
				{
					const TSharedPtr<ISequencer> PinnedSequencer = WeakSequencer.Pin();
					if (!PinnedSequencer)
					{
						return;
					}
					const TSharedRef<ISequencer> SequencerRef = PinnedSequencer.ToSharedRef();

					FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");

					FString NewBindingPath = Sequence->GetPathName();
					FString NewBindingName = Sequence->GetName() + TEXT("_BindingType");
					AssetToolsModule.Get().CreateUniqueAssetName(NewBindingPath + TEXT("/") + NewBindingName, TEXT(""), NewBindingPath, NewBindingName);

					UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprintFromClass(LOCTEXT("CreateNewBindingClass", "Create New Replaceable Binding Class"), UMovieSceneReplaceableActorBinding_BPBase::StaticClass(), NewBindingName);

					if (Blueprint != NULL && Blueprint->GeneratedClass)
					{
						GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);

						// Implement the ResolveRuntimeBindingInternal function

						UFunction* OverrideFunc = FindUField<UFunction>(UMovieSceneReplaceableActorBinding_BPBase::StaticClass(), GET_FUNCTION_NAME_CHECKED(UMovieSceneReplaceableActorBinding_BPBase, BP_ResolveRuntimeBinding));
						check(OverrideFunc);
						Blueprint->Modify();
						// Implement the function graph
						UEdGraph* const NewGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("BP_ResolveRuntimeBinding"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
						FBlueprintEditorUtils::AddFunctionGraph(Blueprint, NewGraph, /*bIsUserCreated=*/ false, UMovieSceneReplaceableActorBinding_BPBase::StaticClass());
						NewGraph->Modify();
						FKismetEditorUtilities::CompileBlueprint(Blueprint);

						// Change or convert the binding
						ChangeBindingTypes(SequencerRef, BindingsToChange, [Blueprint, SequencerRef, Sequence, bConvert, OnBindingsChanged](FGuid BindingID, int32 BindingIndex)
							{
								FMovieScenePossessable* NewPossessable = nullptr;
								if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
								{
									if (bConvert)
									{
										NewPossessable = FSequencerUtilities::ConvertToCustomBinding(SequencerRef, BindingID, TSubclassOf<UMovieSceneCustomBinding>(Blueprint->GeneratedClass), BindingIndex);
									}
									else
									{
										BindingReferences->AddOrReplaceBinding(BindingID,
											NewObject<UMovieSceneCustomBinding>(Sequence->GetMovieScene(), Blueprint->GeneratedClass),
											BindingIndex);
										NewPossessable = Sequence->GetMovieScene()->FindPossessable(BindingID);
									}
								}
								return NewPossessable;
							},
							OnBindingsChanged);


						FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(NewGraph);
						SequencerRef->RefreshSupportedCustomBindingTypes();
					}
				}
		)));

		class FUserCustomBindingFilter : public IClassViewerFilter
		{
		public:

			TArray<const TSubclassOf<UMovieSceneCustomBinding>> UserCustomBindingTypes;

			virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
			{
				return UserCustomBindingTypes.Contains(InClass);
			}

			virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InBlueprint, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
			{
				return InBlueprint->GetNativeParent() && InBlueprint->GetNativeParent()->IsChildOf(UMovieSceneReplaceableActorBinding_BPBase::StaticClass());
			}
		};

		MenuBuilder.BeginSection(TEXT("ChooseCustomBindingClass"), LOCTEXT("ChooseCustomBindingClass", "Choose Custom Binding Class"));
		{
			TSharedPtr<FUserCustomBindingFilter> BindingClassFilter = MakeShared<FUserCustomBindingFilter>();
			BindingClassFilter->UserCustomBindingTypes = UserCustomBindingTypes;

			FClassViewerInitializationOptions Options;
			Options.bShowBackgroundBorder = false;
			Options.bShowUnloadedBlueprints = true;
			Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;

			Options.ClassFilters.Add(BindingClassFilter.ToSharedRef());

			MenuBuilder.AddWidget(FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer").CreateClassViewer(Options,
				FOnClassPicked::CreateLambda([this, WeakSequencer, Sequence, bConvert, BindingsToChange, OnBindingsChanged](UClass* Class)
					{
						const TSharedPtr<ISequencer> PinnedSequencer = WeakSequencer.Pin();
						if (!PinnedSequencer)
						{
							return;
						}
						const TSharedRef<ISequencer> SequencerRef = PinnedSequencer.ToSharedRef();

						// Change or convert the binding
						ChangeBindingTypes(SequencerRef, BindingsToChange, [SequencerRef, Sequence, Class, bConvert, OnBindingsChanged](FGuid BindingID, int32 BindingIndex)
							{
								FMovieScenePossessable* NewPossessable = nullptr;
								if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
								{
									if (bConvert)
									{
										NewPossessable = FSequencerUtilities::ConvertToCustomBinding(SequencerRef, BindingID, TSubclassOf<UMovieSceneCustomBinding>(Class), BindingIndex);
									}
									else
									{
										BindingReferences->AddOrReplaceBinding(BindingID,
											NewObject<UMovieSceneCustomBinding>(Sequence->GetMovieScene(), Class),
											BindingIndex);
										NewPossessable = Sequence->GetMovieScene()->FindPossessable(BindingID);
									}
								}
								return NewPossessable;
							},
							OnBindingsChanged);
						FSlateApplication::Get().DismissAllMenus();
					})), FText::GetEmpty(), true);
		}
		MenuBuilder.EndSection();
	}
}

void ULevelSequenceEditorSubsystem::ChangeBindingTypes(const TSharedRef<ISequencer>& InSequencer
	, const TArray<FSequencerChangeBindingInfo>& InBindingsToChange
	, TFunction<FMovieScenePossessable* (FGuid, int32)> InDoChangeType
	, TFunction<void()> InOnBindingChanged)
{
	using namespace UE::Sequencer;

	UMovieSceneSequence* const Sequence = InSequencer->GetFocusedMovieSceneSequence();
	if (!IsValid(Sequence))
	{
		return;
	}

	UMovieScene* const MovieScene = Sequence->GetMovieScene();
	if (!IsValid(MovieScene))
	{
		return;
	}

	if (InBindingsToChange.Num() == 0)
	{
		return;
	}

	if (MovieScene->IsReadOnly())
	{
		FSequencerUtilities::ShowReadOnlyError();
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ConvertSelectedNodes", "Convert Selected Nodes Binding Type"));
	MovieScene->Modify();

	FScopedSlowTask SlowTask(InBindingsToChange.Num(), LOCTEXT("ConvertProgress", "Converting Selected Nodes Binding Type"));
	SlowTask.MakeDialog(true);

	TArray<AActor*> PossessedActors;
	for (const FSequencerChangeBindingInfo& BindingInfo : InBindingsToChange)
	{
		SlowTask.EnterProgressFrame();

		if (FMovieScenePossessable* Possessable = InDoChangeType(BindingInfo.BindingID, BindingInfo.BindingIndex))
		{
			InSequencer->ForceEvaluate();

			for (TWeakObjectPtr<> WeakObject : InSequencer->FindBoundObjects(Possessable->GetGuid(), InSequencer->GetFocusedTemplateID()))
			{
				if (AActor* PossessedActor = Cast<AActor>(WeakObject.Get()))
				{
					PossessedActors.Add(PossessedActor);
				}
			}

			if (GWarn->ReceivedUserCancel())
			{
				break;
			}
		}
	}

	if (PossessedActors.Num())
	{
		const bool bNotifySelectionChanged = true;
		const bool bDeselectBSP = true;
		const bool bWarnAboutTooManyActors = false;
		const bool bSelectEvenIfHidden = false;

		GEditor->GetSelectedActors()->Modify();
		GEditor->GetSelectedActors()->BeginBatchSelectOperation();
		GEditor->SelectNone(bNotifySelectionChanged, bDeselectBSP, bWarnAboutTooManyActors);
		for (auto PossessedActor : PossessedActors)
		{
			GEditor->SelectActor(PossessedActor, true, bNotifySelectionChanged, bSelectEvenIfHidden);
		}
		GEditor->GetSelectedActors()->EndBatchSelectOperation();
		GEditor->NoteSelectionChange();

		InSequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	}

	if (InOnBindingChanged != nullptr)
	{
		InOnBindingChanged();
	}
}

#undef LOCTEXT_NAMESPACE