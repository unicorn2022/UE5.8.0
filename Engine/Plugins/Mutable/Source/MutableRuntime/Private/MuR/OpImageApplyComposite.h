// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/ImagePrivate.h"

namespace UE::Mutable::Private
{
	void ImageNormalComposite(FImage* ResultImage, const FImage* BaseImage, const FImage* NormalImage, int32 NormalRoughnessOutputChannel, float Power);
}
