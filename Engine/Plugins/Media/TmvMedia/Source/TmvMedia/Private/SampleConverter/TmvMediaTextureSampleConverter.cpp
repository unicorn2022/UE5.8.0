// Copyright Epic Games, Inc. All Rights Reserved.

#include "SampleConverter/TmvMediaTextureSampleConverter.h"

#include "ColorManagement/ColorSpace.h"
#include "ColorManagement/TransferFunctions.h"
#include "MediaShaders.h"
#include "PostProcess/DrawRectangle.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "ScreenPass.h"
#include "TmvMediaLog.h"
#include "TmvStructuredBufferSwizzlingShader.h"
#include "TmvTextureBufferSwizzlingShader.h"
#include "Utils/TmvMediaFrameUtils.h"

DECLARE_GPU_STAT_NAMED(TmvMediaConverter, TEXT("TmvMediaConverter"));
DECLARE_GPU_STAT_NAMED(TmvMediaConverter_MipRender, TEXT("TmvMediaConverter.MipRender"));
DECLARE_GPU_STAT_NAMED(TmvMediaConverter_MipUpscale, TEXT("TmvMediaConverter.MipRender.MipUpscale"));

static bool bTmvConverterTintMipmaps = false;
static FAutoConsoleVariableRef CVarTmvConverterTintMipmaps(
	TEXT("TmvConverter.TintMipmaps"),
	bTmvConverterTintMipmaps,
	TEXT("The TMV sample converter will tint the mipmaps for visualizing (0-Red, 1-Green, 2-blue, 3-Yellow, 4+Pink).\n"),
	ECVF_RenderThreadSafe);

namespace UE::TmvMedia::Converter
{
	/** This function is similar to DrawScreenPass in OpenColorIODisplayExtension.cpp except it is catered for Viewless texture rendering. */
	template<typename TSetupFunction>
	void DrawScreenPass(
		FRHICommandListImmediate& RHICmdList,
		const FIntPoint& OutputResolution,
		const FIntRect& Viewport,
		const FScreenPassPipelineState& PipelineState,
		TSetupFunction SetupFunction)
	{
		RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.f, Viewport.Max.X, Viewport.Max.Y, 1.0f);

		SetScreenPassPipelineState(RHICmdList, PipelineState);

		// Setting up buffers.
		SetupFunction(RHICmdList);

		EDrawRectangleFlags DrawRectangleFlags = EDRF_UseTriangleOptimization;

		UE::Renderer::PostProcess::DrawPostProcessPass(
			RHICmdList,
			PipelineState.VertexShader,
			0, 0, OutputResolution.X, OutputResolution.Y,
			Viewport.Min.X, Viewport.Min.Y, Viewport.Width(), Viewport.Height(),
			OutputResolution,
			OutputResolution,
			INDEX_NONE,
			false,
			DrawRectangleFlags);
	}

	uint32 GetMaxValue(uint8 InBitDepth)
	{
		if (InBitDepth == 0)
		{
			return 0;
		}
		if (InBitDepth >= 32u)
		{
			return 0xFFFFFFFFu;
		}
		return (1u << static_cast<uint32>(InBitDepth)) - 1u;
	}

	/** Normalization factor for _UINT (not normalized) buffer lookup. */
	float GetDataNorm(ETmvMediaFrameComponentType InType, uint8 InBitDepth)
	{
		if (InType == ETmvMediaFrameComponentType::Int)
		{
			return InBitDepth > 0 ? 1.0f/static_cast<float>(GetMaxValue(InBitDepth)) : 1.0f;
		}
		return 1.0f;
	}

	/**
	 * Scale factor for normalized _UNORM texture lookup.
	 * This is scaling the data from a representation in InBitDepth bits to a number of bits aligned to bytes.
	 */
	float GetDataScale(ETmvMediaFrameComponentType InType, uint8 InBitDepth)
	{
		if (InType == ETmvMediaFrameComponentType::Int && InBitDepth > 0)
		{
			// 10 bits max value should scale to 16 bits max value e.g. 1023 should scale to 65535. The scale would be 65535/1023.
			return static_cast<float>(GetMaxValue((InBitDepth + 7) & ~7))/static_cast<float>(GetMaxValue(InBitDepth));
		}
		return 1.0f;
	}
	
	enum class EDataRangeType : uint8
    {
    	/** raw, non-negative whole number. These need to be normalized (or adjusted) to the bit depth. */
    	UInt,
    	/** sampled as floating point normalized between 0-1. These need to be scaled back (or denormalized) to map to bit depth. */
    	UNorm
    };
	
	float GetDataFactor(EDataRangeType InDataRangeType, ETmvMediaFrameComponentType InType, uint8 InBitDepth)
	{
		switch (InDataRangeType)
		{
		case EDataRangeType::UInt:
			return GetDataNorm(InType, InBitDepth);

		case EDataRangeType::UNorm:
		default:
			return GetDataScale(InType, InBitDepth);
		}
	}

	FMatrix CombineColorTransformAndOffset(const FMatrix& InMatrix, const FVector& InYUVOffset, float InDataNorm)
	{
			FMatrix Pre = FMatrix::Identity;
			Pre.M[0][0] = InDataNorm;
			Pre.M[1][1] = InDataNorm;
			Pre.M[2][2] = InDataNorm;
			Pre.M[0][3] = -InYUVOffset.X;
			Pre.M[1][3] = -InYUVOffset.Y;
			Pre.M[2][3] = -InYUVOffset.Z;
			return InMatrix * Pre;
	}

	// Calculating the tile buffer stride, with some extra validation.
	// @remark this value is only used by the shader in tiled mode.
	int32 GetTileBufferFullStride(const FTmvMediaFrameMipInfo& InMipInfo)
	{
		// Note: this should already be caught in the buffer allocation. This is just extra safety.
		
		// In tiled mode, the plane info is per tile.
		// The shader is limited to an int32 for this value.
		// It should be reasonable to expect the tile buffer stride to be small enough to fit in under 2 GB.
		const SIZE_T AllPlaneStride = InMipInfo.GetAllPlaneMemorySizeInBytes();
		if (ensureMsgf(AllPlaneStride <= MAX_int32, TEXT("The tile buffer stride exceed 2 GB. This is not supported by the conversion shader.")))
		{
			return static_cast<int32>(AllPlaneStride);
		}
		return 0;
	}

	static constexpr int NumMipColors = 5;
	static FLinearColor MipmapColorizing[5] =
		{
		{1.0f, 0.0f, 0.0f, 0.5f}, // red
		{0.0f, 1.0f, 0.0f, 0.5f}, // green
		{0.0f, 0.0f, 1.0f, 0.5f}, // blue
		{1.0f, 1.0f, 0.0f, 0.5f}, // yellow
		{1.0f, 0.0f, 1.0f, 0.5f}  // purple
		};
	
	void SetupTintColor(int32 InMipLevel, FTmvMediaShaderColorParameters& OutColorParams)
	{
		if (bTmvConverterTintMipmaps)
		{
			int TintIdx = FMath::Min(NumMipColors-1, InMipLevel);
			OutColorParams.MipTint = MipmapColorizing[TintIdx];
		}
		else
		{
			OutColorParams.MipTint = FLinearColor(1.0f, 1.0f, 1.0f, 0.0f);
		}
	}
	

	void SetupYuvColorParams( 
		const FTmvMediaFrameMipInfo& InMipInfo,
		EDataRangeType InDataRange,
		FTmvMediaShaderColorParameters& OutColorParams)
	{
		OutColorParams.bConvertYUV = (InMipInfo.ColorModel == ETmvMediaFrameColorModel::YUV && InMipInfo.Planes.IsValidIndex(0)) ? 1u : 0u;
		if (OutColorParams.bConvertYUV)
		{
			const ETmvMediaFrameComponentType ComponentType = InMipInfo.Planes[0].Type;
			const uint8 BitDepth = InMipInfo.Planes[0].BitDepth;
			const FVector YUVOffset = FrameUtils::GetYuvOffset(ComponentType, InMipInfo.ColorInfo.YuvMatrixRange, BitDepth);
			const float DataFactor = GetDataFactor(InDataRange, ComponentType, BitDepth);
			const FMatrix YuvToRgbMatrix = FrameUtils::GetYuvToRgbMatrix(InMipInfo.ColorInfo.YuvMatrix, InMipInfo.ColorInfo.YuvMatrixRange);
			OutColorParams.YUVMatrix = static_cast<FMatrix44f>(CombineColorTransformAndOffset(YuvToRgbMatrix, YUVOffset, DataFactor));
		}
		else
		{
			OutColorParams.YUVMatrix = FMatrix44f::Identity;
		}
	}
	
	/** Configure the color related shader parameters. */
	void SetupColorParams( 
		const FTmvMediaTextureSampleConverterParameters& InConverterParams, 
		const FTmvMediaFrameMipInfo& InMipInfo,
		FTmvMediaShaderColorParameters& OutColorParams)
	{
		// Color settings from the decoded media (if available).
		Color::EEncoding SourceEncoding = InMipInfo.ColorInfo.Encoding;
		Color::EReferenceWhite ReferenceWhite = InMipInfo.ColorInfo.ReferenceWhiteOverride;
		Color::FColorSpace SourceColorSpace = Color::FColorSpace(InMipInfo.ColorInfo.ColorSpace);
		Color::EChromaticAdaptationMethod Method = Color::DEFAULT_CHROMATIC_ADAPTATION_METHOD;

		// Apply potential media source color settings overrides.
		if (InConverterParams.SourceColorSettings.IsValid())
		{
			if (InConverterParams.SourceColorSettings->HasEncodingOverride())
			{
				SourceEncoding = InConverterParams.SourceColorSettings->GetEncodingOverride();
			}
			const Color::EReferenceWhite UserReferenceWhite = InConverterParams.SourceColorSettings->GetReferenceWhiteOverride();
			if (UserReferenceWhite != Color::EReferenceWhite::None)
			{
				ReferenceWhite = UserReferenceWhite;
			}
			SourceColorSpace = InConverterParams.SourceColorSettings->GetColorSpaceOverride(SourceColorSpace);
			Method = InConverterParams.SourceColorSettings->GetChromaticAdaptationMethod();
		}

		OutColorParams.bApplyColorTransform = 0u;

		// Note: we always decode to linear, if the destination RT is sRGB, it will encode to that automatically.
		if (SourceEncoding != Color::EEncoding::Linear && SourceEncoding != Color::EEncoding::None)
		{
			OutColorParams.bApplyColorTransform = 1u;
			OutColorParams.EOTF = static_cast<uint32>(SourceEncoding);
		}
		else
		{
			OutColorParams.EOTF = static_cast<uint32>(Color::EEncoding::None);
		}

		// Destination color space is the "working" color space (should be sRGB primaries).
		const UE::Color::FColorSpace& DestinationCS = UE::Color::FColorSpace::GetWorking();

		if (SourceColorSpace.Equals(DestinationCS))
		{
			OutColorParams.ColorSpaceMatrix = FMatrix44f::Identity;
		}
		else
		{
			OutColorParams.bApplyColorTransform = 1u;
			OutColorParams.ColorSpaceMatrix = UE::Color::Transpose<float>(UE::Color::FColorSpaceTransform(SourceColorSpace, DestinationCS, Method));
		}

		// Apply reference-white normalization as a pre-scale on the color-space transform,
		// so the shader's spec-literal transfer-function output (nits for PQ, BT.2100 linear
		// for HLG) is normalized to UE scene-linear. Non-unity factor also forces the color
		// conversion pass to run.
		const float NormalizationFactor = Color::GetReferenceWhiteLinearScale(SourceEncoding, ReferenceWhite);
		if (!FMath::IsNearlyEqual(NormalizationFactor, 1.0f))
		{
			OutColorParams.bApplyColorTransform = 1u;
			OutColorParams.ColorSpaceMatrix = OutColorParams.ColorSpaceMatrix.ApplyScale(NormalizationFactor);
		}
	}

	void RenderMip_PlaneTextures
		( FRHICommandListImmediate& RHICmdList
		, const FTextureRHIRef& RenderTargetTextureRHI
		, const FTmvMediaTextureSampleConverterParameters& ConverterParams
		, int32 TextureMipLevel
		, const FTmvMediaFrameMipBufferHandle& BufferData
		, const FIntPoint& TextureSize
		, const TArray<FIntRect>& MipViewports)
	{
		RHI_BREADCRUMB_EVENT_STAT(RHICmdList, TmvMediaConverter_MipRender, "TmvMediaConverter.MipRender");

		FRHIRenderPassInfo RPInfo(RenderTargetTextureRHI, ERenderTargetActions::DontLoad_Store, nullptr, TextureMipLevel);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("TmvTextureSwizzle"));

		const FTmvMediaFrameMipInfo& MipInfo = BufferData->GetMipInfoRef();
		
		FTmvTextureBufferSwizzlePS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTmvTextureBufferSwizzlePS::FNumComponents>(MipInfo.NumComponents - 1);

		FTmvTextureBufferSwizzlePS::FParameters Parameters = FTmvTextureBufferSwizzlePS::FParameters();

		Parameters.ColorParams.AlphaScale = MipInfo.Planes.Num() == 4 ? GetDataScale(MipInfo.Planes[3].Type, MipInfo.Planes[3].BitDepth) : 1.0f;
		SetupYuvColorParams(MipInfo, EDataRangeType::UNorm, Parameters.ColorParams);
		SetupColorParams(ConverterParams, MipInfo, Parameters.ColorParams);
		SetupTintColor(TextureMipLevel, Parameters.ColorParams);
		
		Parameters.InPlaneTexture0 = BufferData->GetShaderResourceView(0);
		Parameters.InPlaneTexture1 = BufferData->GetShaderResourceView(1);
		Parameters.InPlaneTexture2 = BufferData->GetShaderResourceView(2);
		Parameters.InPlaneTexture3 = BufferData->GetShaderResourceView(3);

		Parameters.InPlaneTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

		TShaderMapRef<FTmvSwizzleVS> SwizzleShaderVS(ShaderMap);
		TShaderMapRef<FTmvTextureBufferSwizzlePS> SwizzleShaderPS(ShaderMap, PermutationVector);

		FScreenPassPipelineState PipelineState(SwizzleShaderVS, SwizzleShaderPS, TStaticBlendState<>::GetRHI(), TStaticDepthStencilState<false, CF_Always>::GetRHI());

		// If there are tiles determines if we should deliver tiles one by one or in a bulk.
		for (const FIntRect& Viewport : MipViewports)
		{
			DrawScreenPass(RHICmdList, TextureSize, Viewport, PipelineState, [&](FRHICommandListImmediate& InRHICmdList)
				{
					SetShaderParameters(InRHICmdList, SwizzleShaderPS, SwizzleShaderPS.GetPixelShader(), Parameters);
				});
		}

		// Resolve render target.
		RHICmdList.EndRenderPass();
	}
	
	void RenderMip_StructuredBuffer
		( FRHICommandListImmediate& RHICmdList
		, const FTextureRHIRef& RenderTargetTextureRHI
		, const FTmvMediaTextureSampleConverterParameters& ConverterParams
		, int32 SampleMipLevel
		, int32 TextureMipLevel
		, const FTmvMediaFrameMipBufferHandle& BufferData
		, const FIntPoint& SampleSize
		, const FIntPoint& TextureSize
		, const TArray<FIntRect>& MipViewports)
	{
		RHI_BREADCRUMB_EVENT_STAT(RHICmdList, TmvMediaConverter_MipRender, "TmvMediaConverter.MipRender");

		FRHIRenderPassInfo RPInfo(RenderTargetTextureRHI, ERenderTargetActions::DontLoad_Store, nullptr, TextureMipLevel);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("TmvTextureSwizzle"));

		const FTmvMediaFrameMipInfo& MipInfo = BufferData->GetMipInfoRef();
		ETmvMediaFrameComponentType ComponentType = MipInfo.Planes.IsValidIndex(0) ? MipInfo.Planes[0].Type : ETmvMediaFrameComponentType::Int;
		
		FTmvStructuredBufferSwizzlePS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTmvStructuredBufferSwizzlePS::FNumComponents>(MipInfo.NumComponents - 1);
		PermutationVector.Set<FTmvStructuredBufferSwizzlePS::FElementFormat>(ComponentType == ETmvMediaFrameComponentType::Float ? 0 : 1);

		// Can have tiles but be planar. Need another parameter for planar vs tiled layout.
		ETmvSwizzleBufferLayouts BufferLayout = ETmvSwizzleBufferLayouts::PlanarFull;
		switch (MipInfo.Layout)
		{
		case ETmvMediaFrameBufferLayout::Tiled:
			// Check if we have tile info for this mip level.
			if (ConverterParams.TileInfoPerMipLevel.Num() > SampleMipLevel && ConverterParams.TileInfoPerMipLevel[SampleMipLevel].Num() > 0)
			{
				// Tiles are resolved through lookup of tile info.
				BufferLayout = ETmvSwizzleBufferLayouts::TiledPartial;
			}
			else
			{
				// If no tile info, we assume it is a standard full tiled layout.
				BufferLayout = ETmvSwizzleBufferLayouts::TiledFull;
			}
			 break;
		case ETmvMediaFrameBufferLayout::ScanLine:
			BufferLayout = ETmvSwizzleBufferLayouts::PlanarFull;
			break;
		}
		PermutationVector.Set<FTmvStructuredBufferSwizzlePS::FBufferLayout>(static_cast<int32>(BufferLayout)); 

		FTmvStructuredBufferSwizzlePS::FParameters Parameters = FTmvStructuredBufferSwizzlePS::FParameters();
		Parameters.DestTextureSize = SampleSize;
		Parameters.TileSize = ConverterParams.TileDimWithBorders;
		Parameters.TileBufferFullStride =  MipInfo.Layout == ETmvMediaFrameBufferLayout::Tiled ? GetTileBufferFullStride(MipInfo) : 0;

		// Setup memory layout of the planes
		for (int32 ComponentIndex = 0; ComponentIndex < MipInfo.NumComponents; ComponentIndex++)
		{
			int32 ComponentIndexInPlane = 0;
			int32 PlaneIndex = MipInfo.GetPlaneIndexForComponent(ComponentIndex, &ComponentIndexInPlane);
			if (MipInfo.Planes.IsValidIndex(PlaneIndex))
			{
				const FTmvMediaFramePlaneInfo& PlaneInfo = MipInfo.Planes[PlaneIndex];

				// Calculating the offset of the component from the start of the mip buffer.
				SIZE_T ComponentOffsetInBytes = 0;
				MipInfo.GetPlaneBufferOffset(PlaneIndex, ComponentOffsetInBytes);
				ComponentOffsetInBytes += PlaneInfo.GetStartComponentOffsetInBytes(ComponentIndexInPlane);
				
				// Note: Shader expects strides and offsets in shorts.
				Parameters.TileBufferOffsets[ComponentIndex] = ComponentOffsetInBytes / 2;	// tiled mode, plane info is per tile.
				Parameters.TileBufferStrides[ComponentIndex] = PlaneInfo.Stride / 2;	// tiled mode, plane info is per tile.
				Parameters.PlaneBufferOffsets[ComponentIndex] = ComponentOffsetInBytes / 2;
				Parameters.PlaneBufferStrides[ComponentIndex] = PlaneInfo.Stride / 2;
				Parameters.PlaneWidthRatios[ComponentIndex] = PlaneInfo.WidthRatio;
				Parameters.PlaneHeightRatios[ComponentIndex] = PlaneInfo.HeightRatio;
			}
		}

		Parameters.ColorParams.AlphaScale = MipInfo.Planes.Num() == 4 ? GetDataNorm(MipInfo.Planes[3].Type, MipInfo.Planes[3].BitDepth) : 1.0f;
		
		SetupYuvColorParams(MipInfo, EDataRangeType::UInt, Parameters.ColorParams);
		SetupColorParams(ConverterParams, MipInfo, Parameters.ColorParams);
		SetupTintColor(TextureMipLevel, Parameters.ColorParams);
	
		if (ConverterParams.bHasTiles)
		{
			Parameters.NumTiles = FIntPoint(FMath::CeilToInt(float(SampleSize.X) / ConverterParams.TileDimWithBorders.X), FMath::CeilToInt(float(SampleSize.Y) / ConverterParams.TileDimWithBorders.Y));
		}
		
		if (ConverterParams.bHasTiles &&
			(ConverterParams.TileInfoPerMipLevel.Num() > SampleMipLevel && ConverterParams.TileInfoPerMipLevel[SampleMipLevel].Num() > 0))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_STR("TmvMediaConverter.TileDesc");

			// This buffer is allocated on already allocated block, therefore the risk of fragmentation is mitigated.
			const FRHIBufferCreateDesc CreateDesc =
				FRHIBufferCreateDesc::CreateStructured<FTmvMediaShaderTileDesc>(TEXT("TmvMediaConverter_TileDesc"), ConverterParams.TileInfoPerMipLevel[SampleMipLevel].Num())
				.AddUsage(EBufferUsageFlags::ShaderResource | EBufferUsageFlags::Dynamic | EBufferUsageFlags::FastVRAM)
				.SetInitActionInitializer()
				.DetermineInitialState();

			TRHIBufferInitializer<FTmvMediaShaderTileDesc> Initializer = RHICmdList.CreateBufferInitializer(CreateDesc);
			Initializer.WriteArray(MakeConstArrayView(ConverterParams.TileInfoPerMipLevel[SampleMipLevel]));

			FBufferRHIRef BufferRef = Initializer.Finalize();

			Parameters.TileMappingBuffer = RHICmdList.CreateShaderResourceView(BufferRef, FRHIViewDesc::CreateBufferSRV().SetTypeFromBuffer(BufferRef));
		}

		Parameters.UnswizzledBuffer = BufferData->GetShaderResourceView(0);

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

		TShaderMapRef<FTmvSwizzleVS> SwizzleShaderVS(ShaderMap);
		TShaderMapRef<FTmvStructuredBufferSwizzlePS> SwizzleShaderPS(ShaderMap, PermutationVector);

		FScreenPassPipelineState PipelineState(SwizzleShaderVS, SwizzleShaderPS, TStaticBlendState<>::GetRHI(), TStaticDepthStencilState<false, CF_Always>::GetRHI());

		// If there are tiles determines if we should deliver tiles one by one or in a bulk.
		for (const FIntRect& Viewport : MipViewports)
		{
			DrawScreenPass(RHICmdList, TextureSize, Viewport, PipelineState, [&](FRHICommandListImmediate& InRHICmdList)
				{
					SetShaderParameters(InRHICmdList, SwizzleShaderPS, SwizzleShaderPS.GetPixelShader(), Parameters);
				});
		}

		// Resolve render target.
		RHICmdList.EndRenderPass();
	}

	void RenderMip
		( FRHICommandListImmediate& RHICmdList
		, const FTextureRHIRef& RenderTargetTextureRHI
		, const FTmvMediaTextureSampleConverterParameters& ConverterParams
		, int32 SampleMipLevel
		, int32 TextureMipLevel
		, const FTmvMediaFrameMipBufferHandle& BufferData
		, const FIntPoint& SampleSize
		, const FIntPoint& TextureSize
		, const TArray<FIntRect>& MipViewports)
	{
		if (!RenderTargetTextureRHI.IsValid() || !BufferData.IsValid())
		{
			return;
		}

		// Using the same transition pattern as ConvertTextureToOutput.
		RHICmdList.Transition(FRHITransitionInfo(RenderTargetTextureRHI, ERHIAccess::Unknown, ERHIAccess::RTV));
		
		if (BufferData->IsStructuredBuffer())
		{
			RenderMip_StructuredBuffer(RHICmdList, RenderTargetTextureRHI, ConverterParams, SampleMipLevel, TextureMipLevel, BufferData, SampleSize, TextureSize, MipViewports);
		}
		else
		{
			RenderMip_PlaneTextures(RHICmdList, RenderTargetTextureRHI, ConverterParams, TextureMipLevel, BufferData, TextureSize, MipViewports);
		}

		RHICmdList.Transition(FRHITransitionInfo(RenderTargetTextureRHI, ERHIAccess::RTV, ERHIAccess::SRVGraphics));
	}


	bool RenderThreadSwizzler(FRHICommandListImmediate& RHICmdList, FTextureRHIRef RenderTargetTextureRHI, TMap<int32, FTmvMediaFrameMipBufferHandle>& MipBuffers, const FTmvMediaTextureSampleConverterParameters ConverterParams)
	{
		RHI_BREADCRUMB_EVENT_STAT_F(RHICmdList, TmvMediaConverter, "TmvMediaConverter.Convert", "TmvMediaConverter.Convert %d", ConverterParams.FrameId);
		int32 MipToUpscale = ConverterParams.UpscaleMip;

		// Upscale to all mips below the mip to upscale.
		if (MipBuffers.Contains(MipToUpscale))
		{
			for (int32 MipLevel = 0; MipLevel <= MipToUpscale; MipLevel++)
			{
				FTmvMediaFrameMipBufferHandle BufferDataToUpscale = MipBuffers[MipToUpscale];
				if (BufferDataToUpscale.IsValid())
				{
					RHI_BREADCRUMB_EVENT_STAT(RHICmdList, TmvMediaConverter_MipUpscale, "TmvMediaConverter.MipRender.MipUpscale");

					TArray<FIntRect> FakeViewport;
					const int MipLevelDiv = 1 << MipLevel;
					const FIntPoint Dim = ConverterParams.FullResolution / MipLevelDiv;
					FakeViewport.Add(FIntRect(FIntPoint(0, 0), Dim));
					RenderMip(RHICmdList, RenderTargetTextureRHI, ConverterParams, MipToUpscale, MipLevel, BufferDataToUpscale, (ConverterParams.FullResolution / (1 << MipToUpscale)), Dim, FakeViewport);
				}
			}
		}
		else if (MipToUpscale != INDEX_NONE)
		{
			UE_LOGF(LogTmvMedia, Warning, "Requested mip could not be found %d", MipToUpscale);
		}

		for (const TPair<int32, TArray<FIntRect>>& MipLevelViewports : ConverterParams.Viewports)
		{
			int32 MipLevel = MipLevelViewports.Key;
			
			// Sanity check.
			if (!MipBuffers.Contains(MipLevel))
			{
				continue;
			}

			FTmvMediaFrameMipBufferHandle BufferData = MipBuffers[MipLevel];
			int MipLevelDiv = 1 << MipLevel;
			FIntPoint Dim = ConverterParams.FullResolution / MipLevelDiv;

			if (BufferData.IsValid() && BufferData->IsValidForRendering())
			{
				// Skip the mip to upscale because it is read and rendered already.
				if (MipLevelViewports.Key == MipToUpscale)
				{
					continue;
				}
				RenderMip(RHICmdList, RenderTargetTextureRHI, ConverterParams, MipLevel, MipLevel, BufferData, Dim, Dim, MipLevelViewports.Value);
			}
		}

		// Doesn't need further conversion so returning false.
		return false;
	}
}

FTmvMediaTextureSampleConverter::~FTmvMediaTextureSampleConverter() = default;

bool FTmvMediaTextureSampleConverter::Convert(FRHICommandListImmediate& RHICmdList, FTextureRHIRef& InDstTexture, const FConversionHints& Hints)
{
	//smell: lock MipBufferCriticalSection to protect MipBuffers or make a copy?
	return UE::TmvMedia::Converter::RenderThreadSwizzler(RHICmdList, InDstTexture, MipBuffers, GetParams_ThreadSafe());
}

bool FTmvMediaTextureSampleConverter::HasMipLevelBuffer(int32 RequestedMipLevel) const
{
	FScopeLock Lock(&MipBufferCriticalSection);
	return MipBuffers.Contains(RequestedMipLevel);
}

FTmvMediaFrameMipBufferHandle FTmvMediaTextureSampleConverter::GetOrCreateMipLevelBuffer(int32 RequestedMipLevel, TFunction<FTmvMediaFrameMipBufferHandle()> AllocatorFunc)
{
	FTmvMediaFrameMipBufferHandle Result;

	{
		FScopeLock Lock(&MipBufferCriticalSection);

		if (FTmvMediaFrameMipBufferHandle* BufferDataPtr = MipBuffers.Find(RequestedMipLevel))
		{
			Result = *BufferDataPtr;
		}
		else
		{
			Result = MipBuffers.Add(RequestedMipLevel, AllocatorFunc());
		}
	}
		
	// Wait for render thread buffer allocations before using resources
	Result->WaitAllocation();

	return Result;
}