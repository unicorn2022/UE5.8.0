// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetasoundNodeConfigurationCustomization.h"

class FGranularNodeConfigurationCustomization : public Metasound::Editor::FMetaSoundNodeConfigurationCustomization
{
public:
	FGranularNodeConfigurationCustomization(TSharedPtr<IPropertyHandle> InStructProperty, TWeakObjectPtr<UMetasoundEditorGraphNode> InNode);

	virtual void OnChildRowAdded(IDetailPropertyRow& ChildRow) override;

private:
	void OnChildPropertyChanged(const FPropertyChangedEvent& InPropertyChangedEvent);

	FString StructPropertyPath;
	TSharedPtr<IPropertyHandle> EnvelopeTypePropertyHandle;
};
