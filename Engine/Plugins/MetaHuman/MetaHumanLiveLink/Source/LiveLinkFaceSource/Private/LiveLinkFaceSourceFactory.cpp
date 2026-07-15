// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkFaceSourceFactory.h"

#include "LiveLinkFaceSource.h"

#define LOCTEXT_NAMESPACE "LiveLinkFaceSourceFactory"

DEFINE_LOG_CATEGORY(LogLiveLinkFaceSourceFactory);

FText ULiveLinkFaceSourceFactory::GetSourceDisplayName() const
{
	return LOCTEXT("SourceDisplayName", "Live Link Face Source");
}

FText ULiveLinkFaceSourceFactory::GetSourceTooltip() const
{
	return LOCTEXT("SourceTooltip", "MetaHuman animation from Live Link Face.");
}

TSharedPtr<ILiveLinkSource> ULiveLinkFaceSourceFactory::CreateSource(const FString& InConnectionString) const
{
	UE_LOGF(LogLiveLinkFaceSourceFactory, Verbose, "Creating source using connection string '%ls'", *InConnectionString);
	return MakeShared<FLiveLinkFaceSource>(InConnectionString);
}

#undef LOCTEXT_NAMESPACE
