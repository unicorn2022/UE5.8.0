// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/IntPoint.h"
#include "Math/IntVector.h"
#include "RHIFwd.h"
#include "SampleConverter/TmvMediaFrameMipBufferFwd.h"
#include "TmvMediaFrameInfo.h"
#include "TmvMediaShaderDefines.h"

#define UE_API TMVMEDIA_API

/**
 * Abstract based class for a Tmv Frame mip buffer.
 * 
 * A mip buffer for a Tmv frame supports partial updates by a decoder that will write some tiles on the
 * cpu buffer. Then the modified regions of the buffer will be uploaded to the gpu to be used by the Tmv
 * Sample converter shaders to also partially update the final converted texture.
 *
 * The memory storage and gpu buffer implementations are abstracted to be able to support different layouts
 * (i.e. tiled vs scanline) depending on the decoder needs.
 */
class FTmvMediaFrameMipBuffer : public TSharedFromThis<FTmvMediaFrameMipBuffer>
{
public:
	/* Constructor */
	FTmvMediaFrameMipBuffer() = default;

	/** Destructor to clean up render resources */
	virtual ~FTmvMediaFrameMipBuffer() = default;

	/** Returns the total allocation size in bytes. */
	SIZE_T GetAllocatedBufferSize() const
	{
		return AllocatedBufferSize;
	}

	/** Returns the descriptor of the memory layout and color format. */
	const FTmvMediaFrameMipInfo& GetMipInfoRef() const
	{
		return MipInfo;
	}

	/**
	 * Requests an allocation of the buffer suitable for the given mip descriptor.
	 * If the allocation is performed on the render thread, the Mapped buffer is not available immediately.
	 * To ensure the allocation is available, call WaitAllocation.
	 * 
	 * @param InMipInfo Descriptor for the allocation.
	 * @return true if the allocation is valid, false otherwise.
	 */
	virtual bool RequestAllocation(const FTmvMediaFrameMipInfo& InMipInfo) = 0;

	/**
	 * If the allocation is done on the render thread, wait for it to be completed.
	 * Should we do this internally in the buffer accessor instead? (probably)
	 */
	virtual void WaitAllocation() = 0;

	/**
	 * Determines from the given allocation descriptor if the current buffer can be used, if it
	 * can, update it.
	 * This is used when recycling buffers from the pool to make sure the buffer is compatible with
	 * the requested memory layout.
	 * 
	 * @param InMipInfo Descriptor for the allocation. 
	 * @return true if the buffer can be used for the given allocation.
	 */
	virtual bool TryUpdateMipInfo(const FTmvMediaFrameMipInfo& InMipInfo) = 0;
	
	/** Access the full mapped buffer. Only available for structured buffer. */
	virtual void* GetMappedBuffer() = 0;

	/**
	 * Returns the start address of the memory plane buffer that contains the given component.
	 * @remark In the case of interleaved components in a memory plane, an additional offset
	 * provided by GetStartComponentOffsetInBytes(), must be applied to the returned value to
	 * get the start of the component within the plane buffer.
	 */
	virtual void* GetPlaneBufferForComponent(int32 InComponentIndex) = 0;

	/**
	 * Returns the shader resource view that contains the given component.
	 * @remark A single shader resource can have multiple components (either packed or interleaved). 
	 * @param InComponentIndex Index of the component to retrieve.
	 * @return Shader resource view of the plane that has this component.
	 */
	virtual FShaderResourceViewRHIRef GetShaderResourceView(int32 InComponentIndex) const = 0;

	/**
	 * Enqueues the render commands to update the buffer regions corresponding to the specified set of tile regions.
	 * @remark Expected to be called from the decoder/reader worker thread.
	 * 
	 * @param InFrameId Id of the current frame being updated. Used for stats and debugging.
	 * @param InTmvMipInfo Current frame mip info.
	 * @param InTileRegions Tile regions (specified in tile coordinates) to update.
	 * @param InTileInfos Lookup table for tile buffer memory layout. This maps tile coordinates with buffer offsets.
	 */
	virtual void CopyTileRegions(int32 InFrameId, const FTmvMediaFrameMipInfo& InTmvMipInfo, TConstArrayView<FIntRect> InTileRegions, TConstArrayView<FTmvMediaShaderTileDesc> InTileInfos) = 0;

	/** Returns true if the buffer can be used for rendering. */
	virtual bool IsValidForRendering() const = 0;

	/** Returns true if the buffer is implemented as a structured buffer. */
	virtual bool IsStructuredBuffer() const
	{
		return false;
	}

protected:
	/** Allocated buffer size, used for debugging. */
	SIZE_T AllocatedBufferSize = 0;
	
	/** Memory frame info this buffer was allocated with. */
	FTmvMediaFrameMipInfo MipInfo;
};

#undef UE_API