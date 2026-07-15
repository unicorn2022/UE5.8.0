// Copyright Epic Games, Inc. All Rights Reserved.

#if PLATFORM_MAC

#include "MetaHumanPhysicalDeviceProvider.h"
#include "MetaHumanPlatformLog.h"
#include "MetaHumanFaceTrackerInterface.h"

#include "IMetalDynamicRHI.h"
#include "Math/UnitConversion.h"
#include "Features/IModularFeatures.h"

#import <Metal/Metal.h>

bool FMetaHumanPhysicalDeviceProvider::GetLUIDs(FString& OutUEPhysicalDeviceLUID, TArray<FString>& OutAllPhysicalDeviceLUIDs)
{
	if (IsRHIMetal())
	{
		IMetalDynamicRHI* DynamicRHI = GetIMetalDynamicRHI();
		id<MTLDevice> MetalDevice = (__bridge id<MTLDevice>)DynamicRHI->RHIGetDevice();
		const uint64 RegistryId = [MetalDevice registryID];

		// NOTE: This LUID is derived from the Metal registryID and will NOT match the LUID
		// reported by MoltenVK in VkPhysicalDeviceIDProperties.deviceLUID. MoltenVK generates
		// a synthetic LUID that is internal to its implementation. Currently this mismatch has
		// no functional impact because FPipeline::PickPhysicalDevice() falls back to letting
		// Titan select its own GPU when matching fails. If multi-GPU device selection is
		// reintroduced, the Titan GPUAPI could be extended to resolve a Metal registryID to
		// the corresponding MoltenVK LUID.
		OutUEPhysicalDeviceLUID = FString::Printf(TEXT("%08x"), static_cast<uint32>(RegistryId & 0x00000000ffffffff));
	}

	const FName& FeatureName = IDepthProcessingMetadataProvider::GetModularFeatureName();
	if (IModularFeatures::Get().IsModularFeatureAvailable(FeatureName))
	{
		IDepthProcessingMetadataProvider& DepthProcessingMetadata = IModularFeatures::Get().GetModularFeature<IDepthProcessingMetadataProvider>(FeatureName);
		return DepthProcessingMetadata.ListPhysicalDeviceLUIDs(OutAllPhysicalDeviceLUIDs);
	}

	return false;
}

int32 FMetaHumanPhysicalDeviceProvider::GetVRAMInMB()
{
	int32 VRAMInMB = -1;

	if (IsRHIMetal())
	{
		IMetalDynamicRHI* DynamicRHI = GetIMetalDynamicRHI();
		id<MTLDevice> MetalDevice = (__bridge id<MTLDevice>)DynamicRHI->RHIGetDevice();
		const uint64 Bytes = [MetalDevice recommendedMaxWorkingSetSize];
		const uint64 MegaBytes = FUnitConversion::Convert(Bytes, EUnit::Bytes, EUnit::Megabytes);
		VRAMInMB = static_cast<int32>(MegaBytes);
	}

	return VRAMInMB;
}

#endif
