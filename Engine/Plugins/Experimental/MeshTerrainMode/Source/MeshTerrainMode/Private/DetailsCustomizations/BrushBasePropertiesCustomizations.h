// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

namespace UE::MeshTerrain
{
	class FBrushBasePropertiesDetails : public IDetailCustomization
	{
	public:
		static TSharedRef<IDetailCustomization> MakeInstance();
		void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	};
}