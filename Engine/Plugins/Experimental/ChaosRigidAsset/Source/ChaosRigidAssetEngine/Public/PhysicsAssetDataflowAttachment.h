// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dataflow/DataflowAttachment.h"
#include "PhysicsAssetDataflowAttachment.generated.h"

UCLASS()
class UPhysicsAssetDataflowAttachment : public UDataflowAttachment
{
	GENERATED_BODY()

public:

	// IDataflowContentOwner
	void WriteDataflowContent(const TObjectPtr<UDataflowBaseContent>& DataflowContent) const override;
	void ReadDataflowContent(const TObjectPtr<UDataflowBaseContent>& DataflowContent) override;
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////

protected:

	// IDataflowContentOwner
	TObjectPtr<UDataflowBaseContent> CreateDataflowContent() override;
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	// UDataflowAttachment
	FString GetPreviewActorPath() const override;
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////

private:
};
