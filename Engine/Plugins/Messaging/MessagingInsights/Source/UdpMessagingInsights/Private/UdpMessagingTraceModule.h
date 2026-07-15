// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/ModuleService.h"


namespace UE::MessagingInsights
{

class FUdpMessagingTraceModule : public TraceServices::IModule
{
	static FName ModuleName;

public:
	//~ Begin TraceServices::IModule interface
	virtual void GetModuleInfo(TraceServices::FModuleInfo& OutModuleInfo) override;
	virtual void OnAnalysisBegin(TraceServices::IAnalysisSession& Session) override;
	//~ End TraceServices::IModule interface
};

} // namespace UE::MessagingInsights
