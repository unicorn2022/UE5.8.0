// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VulkanMemory.h"
#include "VulkanDescriptorSets.h"

// Layout for a Pipeline, also includes DescriptorSets layout
class FVulkanLayout
{
public:
	FVulkanLayout(FVulkanDevice& InDevice, bool InGfxLayout, bool InUsesBindless);
	virtual ~FVulkanLayout();

	const FVulkanDescriptorSetsLayout& GetDescriptorSetsLayout() const
	{
		return DescriptorSetLayout;
	}

	VkPipelineLayout GetPipelineLayout() const
	{
		return PipelineLayout;
	}

	bool HasDescriptors() const
	{
		return DescriptorSetLayout.GetLayouts().Num() > 0;
	}

	uint32 GetDescriptorSetLayoutHash() const
	{
		return DescriptorSetLayout.GetHash();
	}

	bool IsGfxLayout() const
	{
		return bIsGfxLayout;
	}

protected:
	FVulkanDevice& Device;
	FVulkanDescriptorSetsLayout	DescriptorSetLayout;
	VkPipelineLayout			PipelineLayout;

	const bool bIsGfxLayout;
	const bool bUsesBindless;

	void Compile(FVulkanDescriptorSetLayoutMap& DSetLayoutMap);

	friend class FVulkanComputePipeline;
	friend class FVulkanGfxPipeline;
	friend class FVulkanPipelineStateCacheManager;
	friend class FVulkanRayTracingPipelineState;
};
