// Copyright Epic Games, Inc. All Rights Reserved.

#include "FolderCompatibilityWidget.h"

#include "ActorFolders/ActorFolderColumns.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Framework/TypedElementAttributeBinding.h"
#include "Engine/Level.h"
#include "Widgets/Text/STextBlock.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FolderCompatibilityWidget)

#define LOCTEXT_NAMESPACE "FolderCompatibilityWidget"

void UFolderCompatibilityWidgetFactory::RegisterWidgetConstructors(UE::Editor::DataStorage::ICoreProvider& DataStorage,
	UE::Editor::DataStorage::IUiProvider& DataStorageUi) const
{
	DataStorageUi.RegisterWidgetFactory<FFolderCompatibilityWidgetConstructor>(
		DataStorageUi.FindPurpose(DataStorageUi.GetGeneralWidgetPurposeID()),
		UE::Editor::DataStorage::Queries::TColumn<FFolderCompatibilityColumn>());
}

FFolderCompatibilityWidgetConstructor::FFolderCompatibilityWidgetConstructor()
	: Super(StaticStruct())
{
	
}

TSharedPtr<SWidget> FFolderCompatibilityWidgetConstructor::CreateWidget(UE::Editor::DataStorage::ICoreProvider* DataStorage,
	UE::Editor::DataStorage::IUiProvider* DataStorageUi, UE::Editor::DataStorage::RowHandle TargetRow, UE::Editor::DataStorage::RowHandle WidgetRow,
	const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	UE::Editor::DataStorage::FAttributeBinder Binder(TargetRow, DataStorage);
	
	return SNew(STextBlock)
		.Text(Binder.BindData(&FFolderCompatibilityColumn::Folder, [](const FFolder& Folder)
				{
					return FText::FromName(Folder.GetPath());
				}))
		.ToolTipText(Binder.BindTextFormat(
							LOCTEXT("FFolderCompatibilityColumnTooltip", 
								"Path: {Path}\nRootObject: {RootObject}\nActor Folder: {ActorFolder}\nRoot Object Level: {RootObjectLevel}"))
							.Arg(TEXT("Path"), &FTypedElementLabelColumn::Label)
							.Arg(TEXT("RootObject"), &FFolderCompatibilityColumn::Folder, [](const FFolder& Folder)
								{
									if (UObject* Object = Folder.GetRootObjectPtr())
									{
										return FText::FromName(Object->GetFName());
									}
									
									return LOCTEXT("InvalidRootObject", "<Root object not found>");
								})
							.Arg(TEXT("ActorFolder"), &FTypedElementUObjectColumn::Object, [](const TWeakObjectPtr<UObject>&)
							{
								return LOCTEXT("IsActorFolder", "Yes");
							}, LOCTEXT("IsNotActorFolder", "No"))
							.Arg(TEXT("RootObjectLevel"), &FFolderCompatibilityColumn::Folder, [](const FFolder& Folder)
							{
								if (ULevel* Level = Folder.GetRootObjectAssociatedLevel())
								{
									return FText::FromName(Level->GetFName());
								}
								return LOCTEXT("InvalidRootObjectLevel", "<Root object's level not found>");
							}));
}

#undef LOCTEXT_NAMESPACE
