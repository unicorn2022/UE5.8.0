// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ContainersFwd.h"

namespace UE::NNERuntimeCoreML
{
	NNERUNTIMECOREMLUTILS_API bool LoadDirectoryToArray(TArray64<uint8>& Result, const FString& Path);

	NNERUNTIMECOREMLUTILS_API bool SaveArrayToDirectory(TConstArrayView64<uint8> Data, const TCHAR* Path);
};