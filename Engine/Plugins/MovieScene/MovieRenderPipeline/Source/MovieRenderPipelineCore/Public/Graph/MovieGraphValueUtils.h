// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieGraphCommon.h"

namespace UE::MovieGraph::ValueUtils
{
	/** Gets the value type and value type object for a given FProperty. Returns true if the types could be determined, else false. */
	bool GetTypesForFProperty(const FProperty* InProperty, EMovieGraphValueType& OutValueType, TObjectPtr<UObject>& OutValueTypeObject);
}
