// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanFootageIngestModule.h"

#include "CaptureManager.h"
#include "CaptureManagerWidget.h"
#include "MetaHumanFootageRetrievalWindowStyle.h"

#include "Widgets/Docking/SDockTab.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "MetaHumanFootageIngestModule"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

static const FName MetaHumanFootageRetrievalWindowTabName("MetaHumanFootageRetrieval");
static const FName CaptureManagerWindowTabName("CaptureManager");

void FMetaHumanFootageIngestModule::StartupModule()
{
	// Initializes and registers the Footage Retrieval styles
	FMetaHumanFootageRetrievalWindowStyle::Get();
	FMetaHumanFootageRetrievalWindowStyle::Register();
	FMetaHumanFootageRetrievalWindowStyle::ReloadTextures();

	// The legacy capture manager is only functional on Windows
	// Other platforms must use Live Link Hub to perform ingest in 5.8
#if PLATFORM_WINDOWS
	FCaptureManager::Initialize();
	CaptureManager = FCaptureManager::Get();

	// We need to terminate on pre-exit because otherwise we won't be able to do any IsValid() checks
	// on assets while terminating since the asset manager is already deinitialized at that point.
	FCoreDelegates::OnPreExit.AddLambda([this]() {
			FCaptureManager::Terminate();
		});
#endif
}

void FMetaHumanFootageIngestModule::ShutdownModule()
{
	UToolMenus::UnregisterOwner(this);
	
	// Unregister the styles used by the plugin
	FMetaHumanFootageRetrievalWindowStyle::Unregister();
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMetaHumanFootageIngestModule, MetaHumanFootageIngest)