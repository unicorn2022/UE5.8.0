// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameAnimationData.h"
#include "MetaHumanCoreCustomVersion.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FrameAnimationData)


bool FFrameAnimationData::ContainsData() const
{
	return ContainsData(EFrameAnimationDataType::Face);
}

bool FFrameAnimationData::ContainsData(EFrameAnimationDataType InDataType) const
{
	bool bContainsData = false;

	if (EnumHasAnyFlags(InDataType, EFrameAnimationDataType::Face))
	{
		bContainsData |= !AnimationData.IsEmpty();
	}

	if (EnumHasAnyFlags(InDataType, EFrameAnimationDataType::Body))
	{
		bContainsData |= !BodyAnimationData.IsEmpty();
	}

	return bContainsData;
}

void FFrameAnimationData::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FMetaHumanCoreCustomVersion::GUID);

	Ar << Pose;
	Ar << AnimationData;
	Ar << AnimationQuality;

	if (Ar.CustomVer(FMetaHumanCoreCustomVersion::GUID) >= FMetaHumanCoreCustomVersion::AddAudioProcessingTypeToFrameData)
	{
		Ar << AudioProcessingMode;
	}

	if (Ar.CustomVer(FMetaHumanCoreCustomVersion::GUID) >= FMetaHumanCoreCustomVersion::AddBodyAnimationToFrameData)
	{
		Ar << BodyAnimationData;
	}
	
	if (Ar.CustomVer(FMetaHumanCoreCustomVersion::GUID) >= FMetaHumanCoreCustomVersion::AddRawBodyAnimationSMPLXToFrameData && !BodyAnimationData.IsEmpty())
	{
		Ar << RawBodyAnimationSMPLXShape;
		Ar << RawBodyAnimationSMPLXPose;
		Ar << RawBodyAnimationSMPLXTranslation;
		Ar << RawBodyAnimationSMPLXData;
	}
}
