// Copyright Epic Games, Inc. All Rights Reserved.

#include "h265/H265_VideoDecoder_Apple.h"

#ifdef ELECTRA_DECODERS_ENABLE_APPLE

#include "h265/VideoDecoderH265_Apple.h"
#include "ElectraDecodersUtils.h"


class FH265VideoDecoderFactoryApple : public IElectraCodecFactory, public IElectraCodecFactory::IProviderInformation
{
public:
	virtual ~FH265VideoDecoderFactoryApple()
	{}

	void GetConfigurationOptions(TMap<FString, FVariant>& OutOptions) const override
	{
		IElectraVideoDecoderH265_Apple::GetConfigurationOptions(OutOptions);
	}

	int32 SupportsDecoding(TMap<FString, FVariant>& OutFormatInfo, const Electra::FCodecTypeFormat& InCodecFormat, const TMap<FString, FVariant>& InAdditionalOptions) const override
	{
		if (InCodecFormat.Type != Electra::FCodecTypeFormat::EType::Video || !InCodecFormat.Properties.IsType<Electra::FCodecTypeFormat::FVideo>())
		{
			return 0;
		}
		if (InCodecFormat.FourCC != ElectraDecodersUtil::Make4CC('h','v','c','1') && InCodecFormat.FourCC != ElectraDecodersUtil::Make4CC('h','e','v','1'))
		{
			return 0;
		}
		const Electra::FCodecTypeFormat::FVideo& vid = InCodecFormat.Properties.Get<Electra::FCodecTypeFormat::FVideo>();
		const uint32 Width = vid.Width;
		const uint32 Height = vid.Height;
		double fps = vid.FrameRate.IsValid() ? vid.FrameRate.AsDecimal() : 0.0;
		// Check if supported.
		TArray<IElectraVideoDecoderH265_Apple::FSupportedConfiguration> Configs;
		IElectraVideoDecoderH265_Apple::PlatformGetSupportedConfigurations(Configs);
		bool bSupported = false;
		for(int32 i=0; i<Configs.Num(); ++i)
		{
			const IElectraVideoDecoderH265_Apple::FSupportedConfiguration& Cfg = Configs[i];
			if (Cfg.Profile == vid.Profile.Profile)
			{
				if (vid.Profile.Level > Cfg.Level)
				{
					continue;
				}
				if ((Width > Cfg.Width && Cfg.Width) || (Height > Cfg.Height && Cfg.Height))
				{
					continue;
				}
				if (fps > 0.0 && Cfg.FramesPerSecond && (int32)fps > Cfg.FramesPerSecond)
				{
					continue;
				}
				if (Cfg.Num8x8Macroblocks && (((Align(Width, 8) * Align(Height, 8)) / 64) * (fps > 0.0 ? fps : 1.0)) > Cfg.Num8x8Macroblocks)
				{
					continue;
				}
				bSupported = true;
				break;
			}
		}

		return bSupported ? 1 : 0;
	}

	TSharedPtr<IElectraDecoder, ESPMode::ThreadSafe> CreateDecoder(const Electra::FCodecTypeFormat& InCodecFormat, const TMap<FString, FVariant>& InAdditionalOptions) override
	{
		return InCodecFormat.Properties.IsType<Electra::FCodecTypeFormat::FVideo>() ? IElectraVideoDecoderH265_Apple::Create(InCodecFormat, InAdditionalOptions) : TSharedPtr<IElectraDecoder, ESPMode::ThreadSafe>();
	}


	const IProviderInformation& GetProviderInformation() const override
	{ return *this; }
	FString GetName() const override
	{ return FString(TEXT("Video Toolbox")); }
	FString GetVersion() const override
	{ return FString(); }
	FString GetImplementation() const override
	{ return FString(); }
	FString GetVendor() const override
	{ return(FString(TEXT("Apple"))); }
};


TSharedPtr<IElectraCodecFactory, ESPMode::ThreadSafe> FH265VideoDecoderApple::CreateFactory()
{
	return MakeShared<FH265VideoDecoderFactoryApple, ESPMode::ThreadSafe>();
}

#endif
