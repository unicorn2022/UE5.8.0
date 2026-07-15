// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IElectraDecoder.h"


class FElectraDecoderBitstreamProcessorH264 : public IElectraDecoderBitstreamProcessor
{
public:
	static TSharedPtr<IElectraDecoderBitstreamProcessor, ESPMode::ThreadSafe> Create(const TMap<FString, FVariant>& InDecoderParams, const Electra::FCodecTypeFormat& InInitialCodecFormat, const TMap<FString, FVariant>& InAdditionalFormatParams)
	{ return MakeShareable<>(new FElectraDecoderBitstreamProcessorH264(InDecoderParams, InInitialCodecFormat, InAdditionalFormatParams)); }

	virtual ~FElectraDecoderBitstreamProcessorH264();
	bool WillModifyBitstreamInPlace() override;
	void Clear() override;
	EProcessResult ProcessInputForDecoding(TSharedPtr<IElectraDecoderBitstreamInfo, ESPMode::ThreadSafe>& OutBSI, FElectraDecoderInputAccessUnit& InOutAccessUnit, const TMap<FString, FVariant>& InAccessUnitSidebandData) override;
	void SetPropertiesOnOutput(TMap<FString, FVariant>& InOutProperties, TSharedPtr<IElectraDecoderBitstreamInfo, ESPMode::ThreadSafe> InBSI) override;
	FString GetLastError() override;

private:
	ELECTRADECODERS_API FElectraDecoderBitstreamProcessorH264(const TMap<FString, FVariant>& InDecoderParams, const Electra::FCodecTypeFormat& InInitialCodecFormat, const TMap<FString, FVariant>& InAdditionalFormatParams);
	class FImpl;
	TPimplPtr<FImpl> Impl;
};
