// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class IMessageBusTester;
class IMessageBusTesterLogger;

class  IMessageBusTesterModule : public IModuleInterface
{
public:	
	virtual IMessageBusTester& GetMessageBusTester() = 0;

	virtual IMessageBusTesterLogger& GetLogger() = 0;
};

