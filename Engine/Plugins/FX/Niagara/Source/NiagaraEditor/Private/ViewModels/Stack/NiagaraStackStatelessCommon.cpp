// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraStackStatelessCommon.h"

bool FNiagaraStackStatelessCommon::ShouldHideTopLevelCategories(UStruct* Struct)
{
	return Struct ? Struct->HasMetaData(TEXT("ShowTopLevelCategories")) == false : true;
}
