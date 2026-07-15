// Copyright Epic Games, Inc. All Rights Reserved.


#include "CelestialInputDataTypes.h"


// Earth Preset
FPlanetaryBodyInputData FPlanetaryBodyInputData::Earth(TEXT("Earth"), EOrbitType::VSOP87, EVSOP87BodyType::Earth, 6378.0);
// Moon Preset
FPlanetaryBodyInputData FPlanetaryBodyInputData::Moon(TEXT("Moon"), EOrbitType::VSOP87, EVSOP87BodyType::Moon, 1737.4);
