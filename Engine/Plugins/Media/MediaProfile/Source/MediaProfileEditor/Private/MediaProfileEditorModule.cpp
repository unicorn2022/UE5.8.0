// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaProfileEditorModule.h"

#include "Framework/MultiBox/MultiBoxExtender.h"
#include "UI/MediaProfileEditorStyle.h"

#define LOCTEXT_NAMESPACE "FMediaProfileEditorModule"

void FMediaProfileEditorModule::StartupModule()
{
	MediaProfileMenuExtender = MakeShared<FExtender>();
	
	FMediaProfileEditorStyle::Get().Register();
}

void FMediaProfileEditorModule::ShutdownModule()
{
	FMediaProfileEditorStyle::Get().Unregister();
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FMediaProfileEditorModule, MediaProfileEditor)