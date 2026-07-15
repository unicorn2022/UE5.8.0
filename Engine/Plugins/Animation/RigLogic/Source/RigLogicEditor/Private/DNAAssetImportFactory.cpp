// Copyright Epic Games, Inc. All Rights Reserved.

#include "DNAAssetImportFactory.h"
#include "DNAAssetUserData.h"
#include "DNA.h"
#include "DNAAutoConfig.h"

#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Editor.h"

#include "ProfilingDebugging/ScopedTimers.h"

#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "AssetImportTask.h"
#include "EditorFramework/AssetImportData.h"
#include "Misc/FileHelper.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"

#include "DNAImporter.h"
#include "DNAImportSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DNAAssetImportFactory)

DEFINE_LOG_CATEGORY(LogDNAImportFactory);
#define LOCTEXT_NAMESPACE "DNAAssetImportFactory"

UDNAAssetImportFactory::UDNAAssetImportFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = false;
	bEditAfterNew = true;
	SupportedClass = UDNA::StaticClass();

	Formats.Add(TEXT("dna;Character DNA file"));
}

void UDNAAssetImportFactory::PostInitProperties()
{
	Super::PostInitProperties();
	bEditorImport = true;
	bText = false;
}

bool UDNAAssetImportFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	UDNA* DNAAsset = Cast<UDNA>(Obj);
	if (DNAAsset && DNAAsset->AssetImportData)
	{
		FString Filename = DNAAsset->AssetImportData->GetFirstFilename();
		OutFilenames.Add(Filename);

		return !Filename.IsEmpty();
	}

	return false;
}

void UDNAAssetImportFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UDNA* DNAAsset = Cast<UDNA>(Obj);
	if (DNAAsset && ensure(NewReimportPaths.Num() == 1) && DNAAsset->AssetImportData)
	{
		DNAAsset->AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
	}
}

EReimportResult::Type UDNAAssetImportFactory::Reimport(UObject* Obj)
{
	UDNA* DNAAsset = Cast<UDNA>(Obj);
	bool Result = false;

	if(DNAAsset && DNAAsset->AssetImportData->GetFirstFilename().Len() > 0)
	{
		Result = DNAAsset->Init(DNAAsset->AssetImportData->GetFirstFilename(), DNAAsset->DNAConfig);
	} 

	if (Result)
	{
		return EReimportResult::Succeeded;
	}

	return EReimportResult::Failed;
}

UObject* UDNAAssetImportFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	FDNAConfig DNAConfig;
	if (UDNAConfigHolder* DNAConfigHolder = AssetImportTask ? Cast<UDNAConfigHolder>(AssetImportTask->Options) : nullptr)
	{
		DNAConfig = DNAConfigHolder->Config;
	}
	else
	{
		DNAConfig = AutoDetectDNAConfig(Filename);
	}

	UDNA* DNAAsset = NewObject<UDNA>(InParent, InClass, InName, InFlags | RF_Public | RF_Standalone | RF_Transactional);

	// Seed RigLogicConfiguration from project default settings so per-platform calculation type,
	// floating point type, etc. are pre-filled on new assets (same pattern as DefaultDNAConfig above).
	// Must be set before Init() since InitializeRigRuntimeContext() reads RigLogicConfiguration directly.
	DNAAsset->RigLogicConfiguration = GetDefault<UDNAImportSettings>()->DefaultRigLogicConfiguration;

	if (DNAAsset->Init(*Filename, DNAConfig))
	{
		if (!DNAAsset->AssetImportData)
		{
			DNAAsset->AssetImportData = NewObject<UAssetImportData>(DNAAsset);
		}
				
		DNAAsset->AssetImportData->UpdateFilenameOnly(Filename);

		
		FNotificationInfo Info(LOCTEXT("DNA_ImportSuccessMessage", "DNA file successfully imported!"));
		Info.ExpireDuration = 3.0f;
		Info.bFireAndForget = true;
		Info.bUseLargeFont = false;
		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(SNotificationItem::CS_Success);
		}

		GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, DNAAsset);
		return DNAAsset;
	}
	else
	{
		// TODO: Use FMessageLog instead
		const FText Message = LOCTEXT("DNAImportFailed", "DNA asset failed to initialize from given file:");
		FMessageDialog::Open(EAppMsgType::Ok, Message);
		UE_LOGFMT(LogDNAImportFactory, Warning, "{} {}", *Message.ToString(), Filename);
	}

	// Failed to load file or create DNA stream. Clean and return nullptr.
	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, nullptr);
	return nullptr;
}

bool UDNAAssetImportFactory::FactoryCanImport(const FString& Filename)
{
	const FString Extension = FPaths::GetExtension(Filename);

	if (Extension == TEXT("dna"))
	{
		return true;
	}

	return false;
}

#undef LOCTEXT_NAMESPACE

