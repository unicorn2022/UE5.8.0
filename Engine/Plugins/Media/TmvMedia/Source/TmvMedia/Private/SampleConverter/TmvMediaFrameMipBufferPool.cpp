// Copyright Epic Games, Inc. All Rights Reserved.

#include "SampleConverter/TmvMediaFrameMipBufferPool.h"

#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "RenderingThread.h"
#include "SampleConverter/TmvMediaFrameMipBuffer.h"
#include "TmvMediaFrameMipTextureBuffer.h"
#include "TmvMediaFrameMipStructuredBuffer.h"
#include "TmvMediaLog.h"

FTmvMediaFrameMipBufferPool::~FTmvMediaFrameMipBufferPool()
{
	FScopeLock ScopeLock(&MemoryPoolCriticalSection);

	// Copy memory pool array to be released on render thread.
	ENQUEUE_RENDER_COMMAND(DeletePooledBuffers)([InMemoryPool = MemoryPool](FRHICommandListImmediate& RHICmdList)
	{
		SCOPED_DRAW_EVENT(RHICmdList, FTmvMediaFrameMipBuffer_ReleaseMemoryPool);
		TArray<SIZE_T> KeysForIteration;
		InMemoryPool.GetKeys(KeysForIteration);
		for (SIZE_T Key : KeysForIteration)
		{
			TArray<FTmvMediaFrameMipBuffer*> AllValues;
			InMemoryPool.MultiFind(Key, AllValues);
			for (FTmvMediaFrameMipBuffer* MemoryPoolItem : AllValues)
			{
				delete MemoryPoolItem;
			}
		}
	});
}

FTmvMediaFrameMipBufferHandle FTmvMediaFrameMipBufferPool::AcquireBuffer(const FTmvMediaFrameMipInfo& InMipInfo)
{
	const SIZE_T AllocSize = InMipInfo.GetMemorySizeInBytes();;
	
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("TmvConverter.AllocBuffer %llu"), static_cast<uint64>(AllocSize)));
	TWeakPtr<FTmvMediaFrameMipBufferPool, ESPMode::ThreadSafe> BufferPoolWeak = AsWeak();

	// This function is attached to the shared pointer and is used to return any allocated memory to buffer pool.
	auto BufferDeleter = [BufferPoolWeak, AllocSize](FTmvMediaFrameMipBuffer* InBufferToRelease)
	{
		if (TSharedPtr<FTmvMediaFrameMipBufferPool, ESPMode::ThreadSafe> BufferPool = BufferPoolWeak.Pin())
		{
			BufferPool->ReleaseBuffer(AllocSize, InBufferToRelease);
		}
		else
		{
			ENQUEUE_RENDER_COMMAND(DeletePooledBuffers)([InBufferToRelease](FRHICommandListImmediate& RHICmdList)
			{
				delete InBufferToRelease;
			});
		}
	};

	// Buffer that ends up being returned out of this function.
	FTmvMediaFrameMipBufferHandle AllocatedBuffer;

	{
		FScopeLock ScopeLock(&MemoryPoolCriticalSection);
		if (FTmvMediaFrameMipBuffer** FoundBuffer = MemoryPool.Find(AllocSize))
		{
			// Check if the layout is the same, if not we will discard the buffer from the pool.
			if ((*FoundBuffer)->TryUpdateMipInfo(InMipInfo))
			{
				AllocatedBuffer = MakeShareable(*FoundBuffer, MoveTemp(BufferDeleter));
			}
			else
			{
				ENQUEUE_RENDER_COMMAND(DeletePooledBuffers)([BufferToRelease = *FoundBuffer](FRHICommandListImmediate& RHICmdList)
				{
					delete BufferToRelease;
				});
			}
			MemoryPool.Remove(AllocSize, *FoundBuffer);
		}
	}

	if (!AllocatedBuffer)
	{
		FTmvMediaFrameMipBuffer* NewBuffer;
		
		if (InMipInfo.Layout == ETmvMediaFrameBufferLayout::Tiled)
		{
			// If the sample's memory layout is tiled, we use a structured buffer.
			NewBuffer = new FTmvMediaFrameMipStructuredBuffer();
		}
		else
		{
			// If the sample's memory layout is scanline (not tiled), we use a texture buffer.
			NewBuffer = new FTmvMediaFrameMipTextureBuffer();
		}

		if (!NewBuffer->RequestAllocation(InMipInfo))
		{
			delete NewBuffer;
			return nullptr;
		}

		// This buffer will be automatically processed and returned to BufferPool once nothing keeps reference to it.
		AllocatedBuffer = MakeShareable(NewBuffer, MoveTemp(BufferDeleter));
	}

	return AllocatedBuffer;
}

void FTmvMediaFrameMipBufferPool::ReleaseBuffer(SIZE_T InAllocSize, FTmvMediaFrameMipBuffer* InBuffer)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("TmvConverter.ReturnPoolItem");
	FScopeLock ScopeLock(&MemoryPoolCriticalSection);
	MemoryPool.Add(InAllocSize, InBuffer);
}
