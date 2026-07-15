// Copyright Epic Games, Inc. All Rights Reserved.

#include "SandboxMetaDataViewModel.h"

#include "Framework/Models/SandboxInfo.h"
#include "Framework/Models/SandboxSystemModel.h"

namespace UE::SandboxedEditing
{
FSandboxMetaDataViewModel::FSandboxMetaDataViewModel(const TSharedRef<FSandboxSystemModel>& InModel)
	: Model(InModel)
{}

TOptional<FString> FSandboxMetaDataViewModel::GetDescription(const FString& InSandboxRoot) const
{
	const TOptional<FSandboxInfo> Info = Model->GetSandboxInfo(InSandboxRoot);
	return Info ? Info->Description : TOptional<FString>();
}

void FSandboxMetaDataViewModel::SetDescription(const FString& InSandboxRoot, const FString& InNewDescription)
{
	Model->SetDescription(InSandboxRoot, InNewDescription);
}
}
