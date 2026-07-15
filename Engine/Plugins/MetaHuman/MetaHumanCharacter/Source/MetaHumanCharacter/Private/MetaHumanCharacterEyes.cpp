// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEyes.h"

bool FMetaHumanCharacterEyesSettings::operator==(const FMetaHumanCharacterEyesSettings& InOther) const
{
	return EyeLeft == InOther.EyeLeft &&
		EyeRight == InOther.EyeRight;
}

bool FMetaHumanCharacterEyesSettings::operator!=(const FMetaHumanCharacterEyesSettings& InOther) const
{
	return !(*this == InOther);
}

bool FMetaHumanCharacterEyeProperties::operator==(const FMetaHumanCharacterEyeProperties& InOther) const
{
	return Iris == InOther.Iris &&
		Pupil == InOther.Pupil &&
		Cornea == InOther.Cornea &&
		Sclera == InOther.Sclera;
}

bool FMetaHumanCharacterEyeProperties::operator!=(const FMetaHumanCharacterEyeProperties& InOther) const
{
	return !(*this == InOther);
}