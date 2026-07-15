// Copyright Epic Games, Inc. All Rights Reserved.

#include "TmvMediaTranscodeListHandle.h"

void FTmvMediaTranscodeListHandle::SetTranscodeList(UTmvMediaTranscodeList* InTranscodeList)
{
	UTmvMediaTranscodeList* PreviousList = TranscodeListWeak.Get();
	TranscodeListWeak = InTranscodeList;
	OnListChanged.Broadcast(PreviousList, InTranscodeList);
}

