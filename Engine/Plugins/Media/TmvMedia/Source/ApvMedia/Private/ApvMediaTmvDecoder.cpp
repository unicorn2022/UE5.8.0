// Copyright Epic Games, Inc. All Rights Reserved.

#include "ApvMediaTmvDecoder.h"

#include "Apv/ApvDecoder.h"
#include "Apv/ApvMipBufferUtils.h"
#include "Apv/ApvParser.h"
#include "ApvMediaLog.h"
#include "Math/IntRect.h"
#include "Misc/ScopeExit.h"
#include "SampleConverter/TmvMediaFrameMipBuffer.h"

namespace UE::ApvMedia
{
#if OAPV_HAS_SELECTIVE_DECODE_API
	struct FApvAccessUnitInputStream : public oapvd_istream_t
	{
		FApvAccessUnitInputStream(ITmvMediaDecoderAccessUnit* InAccessUnit)
		{
			check(InAccessUnit);
			data = InAccessUnit;
			tell = FApvAccessUnitInputStream::TellImpl;
			seek = FApvAccessUnitInputStream::SeekImpl;
			read = FApvAccessUnitInputStream::ReadImpl;
		}

		static long long TellImpl(oapvd_istream_t* InInputStream)
		{
			return reinterpret_cast<ITmvMediaDecoderAccessUnit*>(InInputStream->data)->Tell();
		}

		static int SeekImpl(oapvd_istream_t* InInputStream, long long InOffset, int InOrigin)
		{
			ITmvMediaDecoderAccessUnit* AccessUnit = reinterpret_cast<ITmvMediaDecoderAccessUnit*>(InInputStream->data);

			bool bSuccess;

			switch (InOrigin)
			{
			case SEEK_CUR:
				bSuccess = AccessUnit->Seek(AccessUnit->Tell() + InOffset);
				break;
			case SEEK_END:
				bSuccess = AccessUnit->Seek(AccessUnit->GetTotalSize() + InOffset);
				break;
			default:
				bSuccess = AccessUnit->Seek(InOffset);
				break;
			}
			return bSuccess ? 0 : -1;
		}

		static size_t ReadImpl(oapvd_istream_t* InInputStream, void* OutBuffer, size_t InSize, size_t InCount)
		{
			if (InSize != 0 && InCount != 0)
			{
				int64 ReadBytes = reinterpret_cast<ITmvMediaDecoderAccessUnit*>(InInputStream->data)->Read(OutBuffer, InSize * InCount);
				return ReadBytes >= 0 ? static_cast<size_t>(ReadBytes)/InSize : 0;	// Return the number of elements (match fread return value).
			}
			return 0;
		}
	};
#endif
	
	void SetupTileRequest(const TArray<FIntRect>& InTileRegions, const FIntPoint& InNumTiles, oapv_mip_request& OutMipRequest)
	{
		OutMipRequest.num_tiles = 0;
		for (const FIntRect& TileRegion : InTileRegions)
		{
			// This clamp is to make sure that tile region is not out of bounds in case the region wasn't calculated incorrectly for some reason.
			const FIntRect& ClampedTileRegion = FIntRect(
				FIntPoint(FMath::Max(TileRegion.Min.X, 0), FMath::Max(TileRegion.Min.Y, 0)),
				FIntPoint(FMath::Min(TileRegion.Max.X, InNumTiles.X), FMath::Min(TileRegion.Max.Y, InNumTiles.Y)));

			for (int32 TileRow = ClampedTileRegion.Min.Y; TileRow < ClampedTileRegion.Max.Y; TileRow++)
			{
				for (int32 TileCol = ClampedTileRegion.Min.X; TileCol < ClampedTileRegion.Max.X; TileCol++)
				{
					// Sanity check.
					if (!ensure(OutMipRequest.num_tiles < OAPV_MAX_TILES))
					{
						return;
					}
	
					int32 TileIdx = OutMipRequest.num_tiles;
					OutMipRequest.tile_coords[TileIdx * 2] = TileCol;
					OutMipRequest.tile_coords[TileIdx * 2 + 1] = TileRow;
					OutMipRequest.num_tiles++;
				}
			}
		}
	}
}

ETmvMediaDecoderResult FApvMediaParser::ParseMipInfos(ITmvMediaDecoderAccessUnit& InAccessUnit, TArray<FTmvMediaFrameMipInfo>& OutMipInfos)
{
	using namespace UE::ApvMedia;
	FApvBitReader BitStreamReader(InAccessUnit.GetFilename(), InAccessUnit.GetUnderlyingArchive());
	if (!BitStreamReader.IsValid())
	{
		return ETmvMediaDecoderResult::Fail;
	}

	// Parse the access unit header
	FApvAccessUnitHeader AccessUnitHeader;
	AccessUnitHeader.Read(BitStreamReader);

	TArray<FApvParserFrameHeader> FrameHeaders; 
	FrameHeaders.Reserve(OAPV_MAX_NUM_FRAMES);
	if (!FApvParser::ParseFrameInfo(BitStreamReader, AccessUnitHeader, FrameHeaders) || FrameHeaders.IsEmpty())
	{
		return ETmvMediaDecoderResult::Fail;
	}

	OutMipInfos.Reserve(FrameHeaders.Num());
	for (const FApvParserFrameHeader& FrameHeader : FrameHeaders)
	{
		const FApvFrameInfo& FrameInfo = FrameHeader.FrameHeader.FrameInfo;
		FTmvMediaFrameMipInfo MipInfo;
		MipInfo.MipLevel = OutMipInfos.Num();
		MipInfo.Height = FrameInfo.Height;
		MipInfo.Width = FrameInfo.Width;
		MipInfo.NumComponents = FrameInfo.GetNumComponents();
		MipInfo.ColorModel = ETmvMediaFrameColorModel::YUV;
		MipInfo.ColorInfo.Encoding = GetColorEncoding(static_cast<EApvMediaColorTransfer>(FrameHeader.FrameHeader.ColorDescription.TransferCharacteristic));
		// APV header's transfer characteristic follows ITU-T H.273, we default to BT.2408 reference white.
		const bool bIsHDR = MipInfo.ColorInfo.Encoding == UE::Color::EEncoding::ST2084 || MipInfo.ColorInfo.Encoding == UE::Color::EEncoding::HLG;
		MipInfo.ColorInfo.ReferenceWhiteOverride = bIsHDR ? UE::Color::EReferenceWhite::BT2408 : UE::Color::EReferenceWhite::None;
		MipInfo.ColorInfo.ColorSpace = GetColorSpace(static_cast<EApvMediaColorSpace>(FrameHeader.FrameHeader.ColorDescription.ColorPrimaries));
		MipInfo.ColorInfo.YuvMatrix = GetColorMatrix(static_cast<EApvMediaColorMatrix>(FrameHeader.FrameHeader.ColorDescription.MatrixCoefficients));
		MipInfo.ColorInfo.YuvMatrixRange = GetColorMatrixRange(static_cast<EApvMediaColorMatrixRange>(FrameHeader.FrameHeader.ColorDescription.FullRangeFlag));
		MipInfo.TileWidth = FrameHeader.FrameHeader.TileInfo.GetTileWidthInSamples();
		MipInfo.TileHeight = FrameHeader.FrameHeader.TileInfo.GetTileHeightInSamples();
		MipInfo.NumTiles = FrameHeader.FrameHeader.GetNumTiles();
		MipInfo.Layout = ETmvMediaFrameBufferLayout::ScanLine;	// Apv Decoder outputs frames in scanline layout.
		MipInfo.Planes.Reserve(FrameInfo.GetNumComponents());
		const int32 NumBytesPerComp = (FrameInfo.BitDepth + 7)/8;	// This is ceil, so 10, 12 bits take a full 16 bits.
		for (int32 ComponentIndex = 0; ComponentIndex < FrameInfo.GetNumComponents(); ++ComponentIndex)
		{
			FTmvMediaFramePlaneInfo PlaneInfo;
			PlaneInfo.NumComponents = 1;
			PlaneInfo.BitDepth = FrameInfo.BitDepth;
			PlaneInfo.Width = FrameInfo.GetPlaneWidth(ComponentIndex, /*bInMBAligned*/ false);
			PlaneInfo.Height = FrameInfo.GetPlaneHeight(ComponentIndex,  /*bInMBAligned*/ false);
			PlaneInfo.Stride = FrameInfo.GetPlaneWidth(ComponentIndex,  /*bInMBAligned*/ true) * NumBytesPerComp;
			PlaneInfo.NumLines = FrameInfo.GetPlaneHeight(ComponentIndex,  /*bInMBAligned*/ true);
			PlaneInfo.WidthRatio = PlaneInfo.Width != 0 ? FrameInfo.Width / PlaneInfo.Width : 1;
			PlaneInfo.HeightRatio = PlaneInfo.Height != 0 ? FrameInfo.Height / PlaneInfo.Height : 1;
			PlaneInfo.Type = ETmvMediaFrameComponentType::Int;
			MipInfo.Planes.Add(PlaneInfo);
		}
		OutMipInfos.Add(MipInfo);
	}

	return ETmvMediaDecoderResult::Success;
}

FApvMediaTmvDecoder::FApvMediaTmvDecoder(int32 InNumDecodeThreads)
{
	DecoderContext = MakeUnique<UE::ApvMedia::FApvDecoderContext>(InNumDecodeThreads);
}

FApvMediaTmvDecoder::~FApvMediaTmvDecoder() = default;

ETmvMediaDecoderResult FApvMediaTmvDecoder::Decode(ITmvMediaDecoderAccessUnit& InAccessUnit, TArrayView<FTmvMediaDecoderMipRequest> InMipRequests)
{
	using namespace UE::ApvMedia;

	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("ApvMedia.Decode %d"), InAccessUnit.GetFrameId()));

	if (!DecoderContext || !DecoderContext->IsValid())
	{
		return ETmvMediaDecoderResult::Fail;
	}

	// Just early out if there is nothing to do.
	if (InMipRequests.IsEmpty())
	{
		return ETmvMediaDecoderResult::Skipped;
	}

#if OAPV_HAS_SELECTIVE_MULTI_MIPS_DECODE_API

	TArray<oapv_mip_request> ApvMipRequests;
	ApvMipRequests.Reserve(InMipRequests.Num());

	// Prepare the mip requests for OpenApv.
	for (const FTmvMediaDecoderMipRequest& MipRequest : InMipRequests)
	{
		if (oapv_imgb_t* OutputBuffer = ApvSetupImageBuffer(MipRequest.MipInfo, *MipRequest.MipBuffer))
		{
			oapv_mip_request& ApvMipRequest = ApvMipRequests.Emplace_GetRef();
			FMemory::Memzero(&ApvMipRequest, sizeof(oapv_mip_request));
			ApvMipRequest.mip_level = MipRequest.MipInfo.MipLevel;
			ApvMipRequest.output_buffer = OutputBuffer;
			SetupTileRequest(MipRequest.TileRegions, MipRequest.MipInfo.NumTiles, ApvMipRequest);
		}
	}

	// Something was invalid in the input requests.
	// Todo: error reporting api.
	if (ApvMipRequests.IsEmpty())
	{
		return ETmvMediaDecoderResult::Fail;
	}

	ON_SCOPE_EXIT
	{
		for (const oapv_mip_request& ApvMipRequest : ApvMipRequests)
		{
			if (ApvMipRequest.output_buffer)
			{
				ApvMipRequest.output_buffer->release(ApvMipRequest.output_buffer);
			}
		}
	};

	FApvAccessUnitInputStream InputStream(&InAccessUnit);

	oapv_multi_mip_decode_t DecodeMultiMip;
	DecodeMultiMip.num_mips = ApvMipRequests.Num();
	DecodeMultiMip.mip_requests = ApvMipRequests.GetData();

	oapvd_stat_t stat;
	FMemory::Memset(&stat, 0, sizeof(oapvd_stat_t));
	
	int ret = oapvd_decode_selective_multi_mips(DecoderContext->did, &InputStream, &DecodeMultiMip, DecoderContext->mid, &stat);
	
	if (OAPV_FAILED(ret))
	{
		UE_LOGF(LogApvMedia, Error, "Failed to decode frame %d in file \"%ls\": Code %d", InAccessUnit.GetFrameId(), *InAccessUnit.GetFilename(), ret);
		return ETmvMediaDecoderResult::Fail;
	}

	// Clear metadata, there is no way to output metadata afaik.
	oapvm_rem_all(DecoderContext->mid);

	// Propagate the results for each mip requests.
	for (const oapv_mip_request& ApvMipRequest : ApvMipRequests)
	{
		FTmvMediaDecoderMipRequest* MipRequest = InMipRequests.FindByPredicate([MipLevel = ApvMipRequest.mip_level](const FTmvMediaDecoderMipRequest& InMipRequest)
		{
			return InMipRequest.MipInfo.MipLevel == MipLevel;
		});
		
		if (MipRequest)
		{
			MipRequest->OutResult = ApvMipRequest.status == OAPV_OK ? ETmvMediaDecoderResult::Success : ETmvMediaDecoderResult::Fail;
			MipRequest->OutNumTilesDecoded = ApvMipRequest.num_tiles;
		}
	}
	return ETmvMediaDecoderResult::Success;
#endif	
	return ETmvMediaDecoderResult::Fail;
}
