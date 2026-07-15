// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utils/IngestLiveLinkDeviceUtils.h"

namespace UE::CaptureManager
{

FString ErrorOriginToString(FTakeMetadataParserError::EOrigin InOrigin)
{
	switch (InOrigin)
	{
		case FTakeMetadataParserError::Reader:
			return TEXT("Reader");
		case FTakeMetadataParserError::Validator:
			return TEXT("Validator");
		case FTakeMetadataParserError::Parser:
		default:
			return TEXT("Parser");
	}
}

}