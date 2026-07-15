// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuR/External/TextureAdapter.h"

#include "MuR/Image.h"

namespace UE::Mutable
{
	FTextureAdapter::FTextureAdapter()
	{
		Image = Private::MakeManaged<Private::FImage>();		
	}


	FTextureAdapter::FTextureAdapter(const FTextureAdapter& Other)
	{
		Image = Private::MakeManaged<Private::FImage>();
		Image->Copy(Other.Image.Get());
	}


	FTextureAdapter& FTextureAdapter::operator=(const FTextureAdapter& Other)
	{
		Image->Copy(Other.Image.Get());
		return *this;
	}
}
