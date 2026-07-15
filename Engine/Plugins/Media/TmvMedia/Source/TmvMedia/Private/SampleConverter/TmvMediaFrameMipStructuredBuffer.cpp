// Copyright Epic Games, Inc. All Rights Reserved.

#include "TmvMediaFrameMipStructuredBuffer.h"

#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "RHICommandList.h"
#include "TmvMediaLog.h"

DECLARE_GPU_STAT_NAMED(TmvConverter_AllocateBuffer, TEXT("TmvConverter.AllocateBuffer"));
DECLARE_GPU_STAT_NAMED(TmvConverter_UploadBufferRegions, TEXT("TmvConverter.UploadBufferRegions"));

static bool bTmvConverterUseUploadHeap = true;

static FAutoConsoleVariableRef CVarTmvConverterUseUploadHeap(
	TEXT("r.TmvConverter.UseUploadHeap"),
	bTmvConverterUseUploadHeap,
	TEXT("Utilizes upload heap and copies raw converter buffer asynchronously.\n")
	TEXT("Read-only and to be set in a config file (requires restart)."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);

FTmvMediaFrameMipStructuredBuffer::FTmvMediaFrameMipStructuredBuffer()
{
	constexpr bool bIsManualReset = true; // Manually reset events stay triggered until reset.
	AllocationReadyEvent = FPlatformProcess::GetSynchEventFromPool(bIsManualReset);
	check(AllocationReadyEvent);
}

FTmvMediaFrameMipStructuredBuffer::~FTmvMediaFrameMipStructuredBuffer()
{
	// Check if the buffer was locked before unlocking.
	if (UploadBufferRef && UploadBufferMapped)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("TmvConverter.ReleasePoolItem");
		FRHICommandListImmediate::Get().UnlockBuffer(UploadBufferRef);
		UploadBufferMapped = nullptr;
	}

	FPlatformProcess::ReturnSynchEventToPool(AllocationReadyEvent);
}

bool FTmvMediaFrameMipStructuredBuffer::RequestAllocation(const FTmvMediaFrameMipInfo& InMipInfo)
{
	MipInfo = InMipInfo;
	AllocatedBufferSize = InMipInfo.GetMemorySizeInBytes();

	// RHI buffer allocation api limitation.
	if (AllocatedBufferSize > MAX_uint32)
	{
		UE_LOGF(LogTmvMedia, Error,
			"Frame Mip Structured Buffers larger than 4 Gb are not supported. Requested size: %llu",
			static_cast<uint64>(AllocatedBufferSize));
		return false;
	}

	// Current shader implementation limitation. The parameters for buffer stride are int32.
	if (InMipInfo.Layout == ETmvMediaFrameBufferLayout::Tiled && InMipInfo.GetAllPlaneMemorySizeInBytes() > static_cast<SIZE_T>(MAX_int32))
	{
		UE_LOGF(LogTmvMedia, Error,
			"The tile buffer stride (%llu bytes) exceed 2 GB limit. This is not supported by the conversion shader.",
			static_cast<uint64>(InMipInfo.GetAllPlaneMemorySizeInBytes()));
		return false;
	}

	// Allocate and unlock the structured buffer on render thread.
	ENQUEUE_RENDER_COMMAND(CreatePooledBuffer)([this, AllocSize = AllocatedBufferSize](FRHICommandListImmediate& RHICmdList)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("TmvConverter.AllocBuffer_RenderThread %llu"), static_cast<uint64>(AllocSize)));

		RHI_BREADCRUMB_EVENT_STAT(RHICmdList, TmvConverter_AllocateBuffer, "TmvConverter.MipRender.AllocateBuffer");

		constexpr uint32 Stride = sizeof(uint16) * 2; // Distance in bytes between the elements of the buffer.

		{
			const FRHIBufferCreateDesc CreateDesc =
				FRHIBufferCreateDesc::CreateStructured(TEXT("TmvConverter.UploadBuffer"), AllocSize, Stride)
				.AddUsage(EBufferUsageFlags::ShaderResource | EBufferUsageFlags::Dynamic | EBufferUsageFlags::FastVRAM)
				.DetermineInitialState();
			UploadBufferRef = RHICmdList.CreateBuffer(CreateDesc);
			UploadBufferMapped = RHICmdList.LockBuffer(UploadBufferRef, 0, AllocSize, RLM_WriteOnly);

			if (bTmvConverterUseUploadHeap)
			{
				// The upload buffer will remain in CopySrc state.
				RHICmdList.Transition(FRHITransitionInfo(UploadBufferRef, CreateDesc.InitialState, ERHIAccess::CopySrc));
			}
		}

		if (bTmvConverterUseUploadHeap)
		{
			const FRHIBufferCreateDesc CreateDesc =
				FRHIBufferCreateDesc::CreateStructured(TEXT("TmvConverter.DestBuffer"), AllocSize, Stride)
				.AddUsage(EBufferUsageFlags::ShaderResource | EBufferUsageFlags::FastVRAM)
				.DetermineInitialState();

			ShaderAccessBufferRef = RHICmdList.CreateBuffer(CreateDesc);
			ShaderResourceView = RHICmdList.CreateShaderResourceView(ShaderAccessBufferRef, FRHIViewDesc::CreateBufferSRV().SetTypeFromBuffer(ShaderAccessBufferRef));
		}
		else
		{
			// Creating a ShaderResourceView (SRV) from a buffer that remains locked (mapped) on the CPU can be invalid.
			// Buffers should typically be unlocked before being used by the GPU as SRVs to avoid synchronization and undefined behavior issues.
			ShaderResourceView = RHICmdList.CreateShaderResourceView(UploadBufferRef, FRHIViewDesc::CreateBufferSRV().SetTypeFromBuffer(UploadBufferRef));
		}

		AllocationReadyEvent->Trigger();
	});
	return true;
}

void FTmvMediaFrameMipStructuredBuffer::WaitAllocation()
{
	// Wait for render thread buffer allocations before using resources
	AllocationReadyEvent->Wait();
}

bool FTmvMediaFrameMipStructuredBuffer::TryUpdateMipInfo(const FTmvMediaFrameMipInfo& InMipInfo)
{
	// For the tiled layout, it is just a big memory buffer, we only care for a compatible size.
	if (MipInfo.Layout != InMipInfo.Layout || InMipInfo.GetMemorySizeInBytes() > AllocatedBufferSize)
	{
		return false;
	}

	MipInfo = InMipInfo;
	return true;
}

void* FTmvMediaFrameMipStructuredBuffer::GetMappedBuffer()
{
	return UploadBufferMapped;
}

void* FTmvMediaFrameMipStructuredBuffer::GetPlaneBufferForComponent(int32 InComponentIndex)
{
	// Only use this in scanline layout. It doesn't make sense in tiled layout.
	ensure(MipInfo.Layout == ETmvMediaFrameBufferLayout::ScanLine);

	SIZE_T PlaneBufferOffset = 0;	
	const int32 PlaneIndex = MipInfo.GetPlaneIndexForComponent(InComponentIndex);
	if (MipInfo.GetPlaneBufferOffset(PlaneIndex, PlaneBufferOffset))
	{
		return static_cast<uint8*>(UploadBufferMapped) + PlaneBufferOffset;
	}
	return nullptr;
}

FShaderResourceViewRHIRef FTmvMediaFrameMipStructuredBuffer::GetShaderResourceView(int32 InComponentIndex) const
{
	return ShaderResourceView;
}

namespace UE::TmvMediaFrameMipStructuredBuffer::Private
{
	struct FBufferRegion
	{
		int64 Offset;
		int64 Size;
	};

	TArray<FBufferRegion> CalculateBufferRegionsToCopy(const FTmvMediaFrameMipInfo& InTmvMipInfo, TConstArrayView<FIntRect> InTileRegions, TConstArrayView<FTmvMediaShaderTileDesc> InTileInfos)
	{
		TArray<FBufferRegion> BufferRegionsToCopy;

		// todo: implementation not completed.
		// See FExrImgMediaReader::ReadTiles.
		
		return BufferRegionsToCopy;
	}
}

void FTmvMediaFrameMipStructuredBuffer::CopyTileRegions(int32 InFrameId, const FTmvMediaFrameMipInfo& InTmvMipInfo, TConstArrayView<FIntRect> InTileRegions, TConstArrayView<FTmvMediaShaderTileDesc> InTileInfos)
{
	using namespace UE::TmvMediaFrameMipStructuredBuffer::Private;

	// If not using the upload buffer, no need to copy.
	if (!bTmvConverterUseUploadHeap)
	{
	 	return;
	}

	// We only support tile regions for tiled layout. It is not possible to update a linear layout with this api.
	// Note: tiled is not implemented at all. We just upload the whole buffer.
	TArray<FBufferRegion> TmpBufferRegionsToCopy = InTmvMipInfo.Layout == ETmvMediaFrameBufferLayout::Tiled ?
		CalculateBufferRegionsToCopy(InTmvMipInfo, InTileRegions, InTileInfos) : TArray<FBufferRegion>();

	TSharedPtr<FTmvMediaFrameMipBuffer> BufferData = AsShared();

	ENQUEUE_RENDER_COMMAND(CopyFromUploadBuffer)([BufferData, BufferRegionsToCopy = MoveTemp(TmpBufferRegionsToCopy), FrameId = InFrameId](FRHICommandListImmediate& InRHICmdList)
	{
		RHI_BREADCRUMB_EVENT_STAT_F(InRHICmdList, TmvConverter_UploadBufferRegions, "TmvMediaConverter.StartCopy", "TmvMediaConverter.StartCopy %d", FrameId);

		FTmvMediaFrameMipStructuredBuffer* Buffer = static_cast<FTmvMediaFrameMipStructuredBuffer*>(BufferData.Get());

		InRHICmdList.Transition(FRHITransitionInfo(Buffer->ShaderAccessBufferRef, ERHIAccess::SRVMask, ERHIAccess::CopyDest));

		if (!BufferRegionsToCopy.IsEmpty())
		{
			for (const FBufferRegion& Region : BufferRegionsToCopy)
			{
				InRHICmdList.CopyBufferRegion(Buffer->ShaderAccessBufferRef, Region.Offset, Buffer->UploadBufferRef, Region.Offset, Region.Size);
			}
		}
		else
		{
			// Copy the whole buffer.
			InRHICmdList.CopyBufferRegion(Buffer->ShaderAccessBufferRef, 0, Buffer->UploadBufferRef, 0, Buffer->ShaderAccessBufferRef->GetSize());		
		}

		// Make sure resource is in SRV mode again.
		InRHICmdList.Transition(FRHITransitionInfo(Buffer->ShaderAccessBufferRef, ERHIAccess::CopyDest, ERHIAccess::SRVMask));
	});
}

bool FTmvMediaFrameMipStructuredBuffer::IsValidForRendering() const
{
	if (!UploadBufferRef.IsValid())
	{
		return false;
	}
	if (bTmvConverterUseUploadHeap && (!ShaderAccessBufferRef.IsValid() || !ShaderAccessBufferRef->IsValid()))
	{
		return false;
	}
	return true;
}