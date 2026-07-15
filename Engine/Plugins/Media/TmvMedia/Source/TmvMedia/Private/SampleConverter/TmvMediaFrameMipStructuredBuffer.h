// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SampleConverter/TmvMediaFrameMipBuffer.h"

/**
 * Implementation of a structured buffer that can be memory mapped and partially updated.
 * This is best suited for tiled memory layouts.
 */
class FTmvMediaFrameMipStructuredBuffer : public FTmvMediaFrameMipBuffer
{
public:
	/* Constructor */
	FTmvMediaFrameMipStructuredBuffer();

	/** Destructor to clean up render resources */
	virtual ~FTmvMediaFrameMipStructuredBuffer() override;

	//~ Begin FTmvMediaFrameMipBuffer
	virtual bool RequestAllocation(const FTmvMediaFrameMipInfo& InMipInfo) override;	
	virtual void WaitAllocation() override;
	virtual bool TryUpdateMipInfo(const FTmvMediaFrameMipInfo& InMipInfo) override;
	virtual void* GetMappedBuffer() override;
	virtual void* GetPlaneBufferForComponent(int32 InComponentIndex) override;
	virtual FShaderResourceViewRHIRef GetShaderResourceView(int32 InComponentIndex) const override;
	virtual void CopyTileRegions(int32 InFrameId, const FTmvMediaFrameMipInfo& InTmvMipInfo, TConstArrayView<FIntRect> InTileRegions, TConstArrayView<FTmvMediaShaderTileDesc> InTileInfos) override;
	virtual bool IsValidForRendering() const override;
	virtual bool IsStructuredBuffer() const override
	{
		return true;
	}
	//~ End FTmvMediaFrameMipBuffer

private:
	/**
	* This is the actual buffer reference that we need to keep after it is locked and until it is unlocked.
	* The buffer is used as an upload heap and will not be accessed by shader if CVar "r.TmvConverter.UseUploadHeap" is set.
	*/
	FBufferRHIRef UploadBufferRef;

	/** 
	* A pointer to mapped GPU memory.
	*/
	void* UploadBufferMapped = nullptr;

	/**
	* This buffer is used by the swizzling shader if CVar "r.TmvConverter.UseUploadHeap" is set and UploadBufferRef contents are copied into it.
	*/
	FBufferRHIRef ShaderAccessBufferRef;

	/** 
	* Resource View used by swizzling shader.
	*/
	FShaderResourceViewRHIRef ShaderResourceView;

	/** Event used to wait for completed buffer allocations. */
	FEvent* AllocationReadyEvent;
};