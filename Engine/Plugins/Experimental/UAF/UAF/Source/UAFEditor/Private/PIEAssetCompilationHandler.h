// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOutOfDateAssetCompilation.h"

namespace UE::UAF::Editor
{

class FOutOfDateAssetCompilation : public PIE::IOutOfDateAssetCompilation
{
private:
	virtual bool HandleOutOfDateAssets_Internal(const FRequestPlaySessionParams& InPlaySessionParams, TArray<UObject*>& OutAssetsWithErrors) const override;
};
}
