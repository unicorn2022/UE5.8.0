// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseMediaSourceColorSettings.h"
#include "ColorManagement/ColorSpace.h"
#include "IMediaTextureSampleConverter.h"
#include "SampleConverter/TmvMediaFrameMipBuffer.h"
#include "TmvMediaShaderDefines.h"

#define UE_API TMVMEDIA_API

/*
* Tmv Sample conversion parameters.
*/
struct FTmvMediaTextureSampleConverterParameters
{
	/** Does this frame has tiles? */
	bool bHasTiles = false;

	/** Frame Id. */
	int32 FrameId = 0;

	/** Resolution of the highest quality mip. */
	FIntPoint FullResolution = FIntPoint(0, 0);

	/** Dimension of the tile including the overscan borders. */
	FIntPoint TileDimWithBorders = FIntPoint(0, 0);

	/** Used for rendering tiles in bulk regions per mip level. */
	TSortedMap<int32, TArray<FIntRect>> Viewports;

	/**
	 * Contain information about individual tiles. Used to convert buffer data into a 2D Texture.
	 * The size of this array is the exact number of complete and partial tiles for each mip level.
	 */
	TArray<TArray<FTmvMediaShaderTileDesc>> TileInfoPerMipLevel;

	/** A lower quality mip will be upscaled if value is 0 or above. At 0 highest quality mip will always be read fully. */
	int32 UpscaleMip = -1;

	/** Manual source color space & encoding overrides. */
	TSharedPtr<FNativeMediaSourceColorSettings, ESPMode::ThreadSafe> SourceColorSettings;
};

/**
 * Implementation of a sample converter for Tmv Frames.
 * 
 * It supports:
 * - Converting planar formats (i.e. components separated in memory planes) with either tiled or scanline memory layouts.
 * - Partial and incremental updates of the buffer regions.
 * - YUV color model
 * - Debugging features like mip tinting.
 */
class FTmvMediaTextureSampleConverter : public IMediaTextureSampleConverter
{
public:
	UE_API virtual ~FTmvMediaTextureSampleConverter() override;

	//~ Begin IMediaTextureSampleConverter
	UE_API virtual bool Convert(FRHICommandListImmediate& RHICmdList, FTextureRHIRef& InDstTexture, const FConversionHints& Hints) override;
	//~ End IMediaTextureSampleConverter

	/** Returns true if the given mip level already has a cached mip buffer in the converter. */
	UE_API bool HasMipLevelBuffer(int32 RequestedMipLevel) const;

	/** A Mip Buffer for the given mip level will either be acquired from the internal cache if available or created using the provided function if not. */ 
	UE_API FTmvMediaFrameMipBufferHandle GetOrCreateMipLevelBuffer(int32 RequestedMipLevel, TFunction<FTmvMediaFrameMipBufferHandle()> AllocatorFunc);

	/** Safely get a copy of the converter parameters. */
	FTmvMediaTextureSampleConverterParameters GetParams_ThreadSafe()
	{
		FScopeLock ScopeLock(&ParamsCriticalSection);
		return ConverterParams;
	}

	/** Safely set the converter parameters. */
	void SetParams_ThreadSafe(const FTmvMediaTextureSampleConverterParameters& InParams)
	{
		FScopeLock ScopeLock(&ParamsCriticalSection);
		ConverterParams = InParams;
	}

private:
	/** These are all required parameters to convert the buffer into texture successfully. */
	mutable FCriticalSection ParamsCriticalSection;
	FTmvMediaTextureSampleConverterParameters ConverterParams;

	/** Lock to be used exclusively on reader threads.*/
	mutable FCriticalSection MipBufferCriticalSection;

	/** Cached mip buffers that will be used in the render thread for updating the final texture. */
	TMap<int32,FTmvMediaFrameMipBufferHandle> MipBuffers;
};

#undef UE_API