// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MP4Boxes.h"

namespace MP4Boxes
{

	/****************************************************************************************************************************************************/

	struct FMP4TrackMetadataCommon
	{
		FString LanguageCode;	// 3 letter ISO 639-2T
		FString LanguageTag;	// BCP47 language tag if available
		FString Name;
		FString HandlerName;
	};

} // namespace MP4Boxes
