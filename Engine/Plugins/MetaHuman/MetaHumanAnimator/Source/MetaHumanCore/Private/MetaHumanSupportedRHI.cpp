// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanSupportedRHI.h"
#include "MetaHumanCoreLog.h"
#include "DynamicRHI.h"

#define LOCTEXT_NAMESPACE "MetaHumanCore"

namespace
{
	TAutoConsoleVariable<bool> CVarCheckRHI{
		TEXT("mh.Core.CheckRHI"),
		true,
		TEXT("If set to true, restricts processing to RHIs known to be supported"),
		ECVF_Default
	};
}

bool FMetaHumanSupportedRHI::bIsInitialized = false;
bool FMetaHumanSupportedRHI::bIsSupported = false;

bool FMetaHumanSupportedRHI::IsSupported()
{
	if (!bIsInitialized && GDynamicRHI) // Dont initialize too early, ensure a RHI is set
	{
		bIsInitialized = true;

		if (CVarCheckRHI.GetValueOnAnyThread())
		{
#if PLATFORM_WINDOWS
			bIsSupported = GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::D3D12;
#elif PLATFORM_LINUX
			bIsSupported = GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::Vulkan;
#elif PLATFORM_MAC
			bIsSupported = GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::Metal;
#elif
			bIsSupported = false;
#endif
		}
		else
		{
			bIsSupported = true;
			UE_LOGF(LogMetaHumanCore, Display, "RHI check disabled");
		}
	}

	return bIsSupported;
}

FText FMetaHumanSupportedRHI::GetSupportedRHINames()
{
#if PLATFORM_WINDOWS
	return LOCTEXT("SupportedRHIDX12", "DirectX 12");
#elif PLATFORM_LINUX
	return LOCTEXT("SupportedRHIVulkan", "Vulkan");
#elif PLATFORM_MAC
	return LOCTEXT("SupportedRHIMetal", "Metal");
#elif
	return LOCTEXT("SupportedRHIUnknown", "Unknown");
#endif
}

void FMetaHumanSupportedRHI::Reset()
{
	bIsInitialized = false;
}

#undef LOCTEXT_NAMESPACE