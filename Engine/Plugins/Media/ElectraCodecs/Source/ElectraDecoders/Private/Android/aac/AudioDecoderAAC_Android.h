// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "IElectraDecoder.h"

class IElectraAudioDecoderAAC_Android : public IElectraDecoder
{
public:
	static void GetConfigurationOptions(TMap<FString, FVariant>& OutOptions);
	static TSharedPtr<IElectraDecoder, ESPMode::ThreadSafe> Create(const Electra::FCodecTypeFormat& InCodecFormat, const TMap<FString, FVariant>& InAdditionalOptions);

	virtual ~IElectraAudioDecoderAAC_Android() = default;
};
