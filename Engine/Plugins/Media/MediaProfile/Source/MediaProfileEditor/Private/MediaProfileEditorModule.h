// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMediaProfileEditorModule.h"

class FExtender;

class FMediaProfileEditorModule : public IMediaProfileEditorModule
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    
    virtual TSharedPtr<FExtender> GetMediaProfileMenuExtender() const override { return MediaProfileMenuExtender; }
    
private:
    TSharedPtr<FExtender> MediaProfileMenuExtender;
};
