// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/ModuleService.h"

namespace UE
{
namespace RenderTraceInsights
{

DECLARE_LOG_CATEGORY_EXTERN(LogRenderTrace, Log, All);

class FRenderTraceModule : public TraceServices::IModule
{
public:
	//~ Begin TraceServices::IModule interface
	virtual void GetModuleInfo(TraceServices::FModuleInfo& OutModuleInfo) override;
	virtual void OnAnalysisBegin(TraceServices::IAnalysisSession& Session) override;
	virtual void GetLoggers(TArray<const TCHAR *>& OutLoggers) override;
	virtual const TCHAR* GetCommandLineArgument() override { return TEXT("rendertrace"); }
	//~ End TraceServices::IModule interface

private:
	static FName ModuleName;
};

} //namespace RenderTraceInsights
} //namespace UE
