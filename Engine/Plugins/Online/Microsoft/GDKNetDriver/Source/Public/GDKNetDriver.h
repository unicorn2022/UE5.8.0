// Copyright Epic Games, Inc. All Rights Reserved.

//
// GDK based implementation of the net driver
//

#pragma once
#include "IpNetDriver.h"
#include "GDKNetDriver.generated.h"


UCLASS( transient, config=Engine )
class UGDKNetDriver : public UIpNetDriver
{
	GENERATED_UCLASS_BODY()

	virtual int GetClientPort() override;
};
