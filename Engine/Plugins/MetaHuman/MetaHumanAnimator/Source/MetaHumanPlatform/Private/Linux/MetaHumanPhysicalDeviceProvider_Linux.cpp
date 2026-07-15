// Copyright Epic Games, Inc. All Rights Reserved.

#if PLATFORM_LINUX

#include "MetaHumanPhysicalDeviceProvider.h"
#include "MetaHumanPlatformLog.h"
#include "MetaHumanFaceTrackerInterface.h"

#include "IVulkanDynamicRHI.h"
#include "Features/IModularFeatures.h"

bool FMetaHumanPhysicalDeviceProvider::GetLUIDs(FString& OutUEPhysicalDeviceLUID, TArray<FString>& OutAllPhysicalDeviceLUIDs)
{
	IVulkanDynamicRHI* RHI = ((GDynamicRHI && GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::Vulkan) ? GetIVulkanDynamicRHI() : nullptr);
	if (!RHI)
	{
		UE_LOGF(LogMetaHumanPlatform, Warning, "Unable to enumerate GPUs - unsupported RHI");
		return false;
	}

	uint32 LUID = (uint32) (RHI->RHIGetGraphicsAdapterLUID(RHI->RHIGetVkPhysicalDevice()) & 0x00000000ffffffff);
	OutUEPhysicalDeviceLUID = FString::Printf(TEXT("%08x"), LUID);

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

	if (GDynamicRHI && GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::Vulkan)
	{
		IVulkanDynamicRHI* DynamicRHI = GetIVulkanDynamicRHI();

		PFN_vkGetPhysicalDeviceMemoryProperties Fn = (PFN_vkGetPhysicalDeviceMemoryProperties)DynamicRHI->RHIGetVkInstanceProcAddr("vkGetPhysicalDeviceMemoryProperties");
		if (Fn)
		{
			VkPhysicalDeviceMemoryProperties MemProps;
			(*Fn)(DynamicRHI->RHIGetVkPhysicalDevice(), &MemProps);

			VRAMInMB = 0;

			for (uint32 HeapIndex = 0; HeapIndex < MemProps.memoryHeapCount; ++HeapIndex)
			{
				VkMemoryHeap Heap = MemProps.memoryHeaps[HeapIndex];

				if (Heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
				{
					VRAMInMB += Heap.size / (1024 * 1024);
				}
			}
		}
	}

	return VRAMInMB;
}

#endif