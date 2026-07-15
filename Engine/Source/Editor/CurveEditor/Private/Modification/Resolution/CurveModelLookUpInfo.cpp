// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modification/Resolution/CurveModelLookUpInfo.h"

#include "CurveEditor.h"
#include "CurveModelIdCache.h"

namespace UE::CurveEditor
{
const FCurveMetaDataIdentifiers* FCurveModelLookUpArgs::GetCurveMetaData() const
{
	FCurveModelIdCache* Cache = CurveEditor.GetCurveMetaDataCache();
	checkSlow(Cache); // Did not call FCurveEditor::InitCurveEditor?
	return Cache ? FindMetaData(*Cache, CurveID) : nullptr;
}
}
