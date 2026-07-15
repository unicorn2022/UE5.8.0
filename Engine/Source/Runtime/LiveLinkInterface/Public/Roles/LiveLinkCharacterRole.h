// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Roles/LiveLinkAnimationRole.h"

#include "Internationalization/Text.h"
#include "LiveLinkFrameInterpolationProcessor.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "LiveLinkCharacterRole.generated.h"

/**
 * Role associated for streaming animation and component transform.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Character Role"), MinimalAPI)
class ULiveLinkCharacterRole : public ULiveLinkAnimationRole
{
	GENERATED_BODY()

public:
	//~ Begin ULiveLinkRole interface
	LIVELINKINTERFACE_API virtual UScriptStruct* GetStaticDataStruct() const override;
	LIVELINKINTERFACE_API virtual UScriptStruct* GetFrameDataStruct() const override;
	LIVELINKINTERFACE_API virtual UScriptStruct* GetBlueprintDataStruct() const override;

	LIVELINKINTERFACE_API virtual FText GetDisplayName() const override;
	LIVELINKINTERFACE_API virtual bool IsStaticDataValid(const FLiveLinkStaticDataStruct& InStaticData, bool& bOutShouldLogWarning) const override;
	LIVELINKINTERFACE_API virtual bool IsFrameDataValid(const FLiveLinkStaticDataStruct& InStaticData, const FLiveLinkFrameDataStruct& InFrameData, bool& bOutShouldLogWarning) const override;
	//~ End ULiveLinkRole interface
};
