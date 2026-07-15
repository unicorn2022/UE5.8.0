// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FVoiceChatModule : public IModuleInterface
{
};

IMPLEMENT_MODULE(FVoiceChatModule, VoiceChat)