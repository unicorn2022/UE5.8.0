// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCrowdSkeletonPickerDialog.h"

#include "MetaHumanCollection.h"
#include "MetaHumanCrowdEditorLog.h"

#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetData.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "Dialog/SCustomDialog.h"
#include "IAssetTools.h"
#include "IContentBrowserSingleton.h"
#include "Logging/StructuredLog.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MetaHumanCrowdSkeletonPickerDialog"

namespace UE::MetaHuman::CrowdEditor
{

namespace Private
{

static USkeleton* PromptPickExistingSkeleton()
{
	FOpenAssetDialogConfig OpenConfig;
	OpenConfig.DialogTitleOverride = LOCTEXT("PickExistingSkeletonTitle", "Choose Target Skeleton");
	OpenConfig.AssetClassNames.Add(USkeleton::StaticClass()->GetClassPathName());
	OpenConfig.bAllowMultipleSelection = false;

	const FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	const TArray<FAssetData> ChosenAssets = ContentBrowserModule.Get().CreateModalOpenAssetDialog(OpenConfig);

	if (ChosenAssets.Num() == 0)
	{
		return nullptr;
	}

	return Cast<USkeleton>(ChosenAssets[0].GetAsset());
}

static USkeleton* PromptCreateSkeletonFromTemplate(USkeleton* TemplateSkeleton, const FString& DefaultCreatePath)
{
	check(TemplateSkeleton);

	FSaveAssetDialogConfig SaveConfig;
	SaveConfig.DialogTitleOverride = LOCTEXT("CreateSkeletonTitle", "Create Target Skeleton From Template");
	SaveConfig.AssetClassNames.Add(USkeleton::StaticClass()->GetClassPathName());
	SaveConfig.DefaultPath = !DefaultCreatePath.IsEmpty()
		? DefaultCreatePath
		: FPaths::GetPath(TemplateSkeleton->GetPathName());
	
	SaveConfig.DefaultAssetName = TemplateSkeleton->GetName();
	if (SaveConfig.DefaultAssetName.EndsWith(TEXT("_Template")))
	{
		SaveConfig.DefaultAssetName = SaveConfig.DefaultAssetName.Left(SaveConfig.DefaultAssetName.Len() - FString(TEXT("_Template")).Len());
	}

	SaveConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::Disallow;

	const FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	const FString SaveObjectPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveConfig);

	if (SaveObjectPath.IsEmpty())
	{
		return nullptr;
	}

	const FString PackageName = FPackageName::ObjectPathToPackageName(SaveObjectPath);
	const FString PackagePath = FPackageName::GetLongPackagePath(PackageName);
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);

	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UObject* DuplicatedAsset = AssetTools.DuplicateAsset(AssetName, PackagePath, TemplateSkeleton);

	if (!DuplicatedAsset)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error,
			"Failed to duplicate target skeleton template {Template} to {Path}",
			TemplateSkeleton->GetPathName(), SaveObjectPath);
		return nullptr;
	}

	USkeleton* DuplicatedSkeleton = Cast<USkeleton>(DuplicatedAsset);
	if (!DuplicatedSkeleton)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error,
			"Duplicated asset at {Path} is not a USkeleton (got {Class}).",
			DuplicatedAsset->GetPathName(), DuplicatedAsset->GetClass()->GetName());
	}

	return DuplicatedSkeleton;
}

} // namespace Private

bool PromptForTargetSkeleton(USkeleton* TemplateSkeleton, const FString& DefaultCreatePath, USkeleton*& OutSkeleton)
{
	OutSkeleton = nullptr;

	if (!TemplateSkeleton)
	{
		return false;
	}

	const FText DialogTitle = LOCTEXT("MissingTargetSkeletonTitle", "Target Skeleton Required");
	const FText DialogMessage = LOCTEXT("MissingTargetSkeletonMessage",
		"The Crowd pipeline requires a skeleton that includes the head and body.\n\n"
		"Importing animations can cause the skeleton to be edited, so it should be stored somewhere in your project or a plugin where editable assets are allowed.");

	// Button order matches the SCustomDialog::FButton array below; ShowModal returns the index.
	enum
	{
		Button_AutoCreate  = 0,
		Button_UseExisting = 1,
		Button_Cancel      = 2,
	};

	const TSharedRef<SCustomDialog> Dialog = SNew(SCustomDialog)
		.Title(DialogTitle)
		.Content()
		[
			SNew(STextBlock)
				.Text(DialogMessage)
				.AutoWrapText(true)
		]
		.Buttons({
			SCustomDialog::FButton(LOCTEXT("AutoCreateCrowdSkeletonButton", "Auto-create crowd skeleton"))
				.SetButtonRole(SCustomDialog::EButtonRole::Confirm),
			SCustomDialog::FButton(LOCTEXT("UseExistingCrowdSkeletonButton", "Use existing crowd skeleton")),
			SCustomDialog::FButton(LOCTEXT("CancelButton", "Cancel"))
				.SetButtonRole(SCustomDialog::EButtonRole::Cancel)
		});

	const int32 Choice = Dialog->ShowModal(Button_Cancel);

	switch (Choice)
	{
	case Button_UseExisting:
		OutSkeleton = Private::PromptPickExistingSkeleton();
		break;

	case Button_AutoCreate:
		OutSkeleton = Private::PromptCreateSkeletonFromTemplate(TemplateSkeleton, DefaultCreatePath);
		break;

	default:
		// Cancel / dialog closed
		break;
	}

	return OutSkeleton != nullptr;
}

bool PromptToAddCompatibleSkeletons(
	TNotNull<const UMetaHumanCollection*> Collection,
	USkeleton* TargetSkeleton,
	const TArray<USkeleton*>& SkeletonsToConsider)
{
	if (!TargetSkeleton)
	{
		return false;
	}

	// Build a set of paths that are already declared compatible by TargetSkeleton itself.
	//
	// We deliberately do NOT use USkeleton::IsCompatibleForEditor here for two reasons:
	//
	//   1. It short-circuits on the AreAllSkeletonsCompatibleDelegate global, which is bound
	//      to UPersonaOptions::bAllowIncompatibleSkeletonSelection -- a local editor
	//      preference. Pipeline behaviour must not depend on per-user editor settings.
	//
	//   2. It also returns true when the OTHER skeleton lists TargetSkeleton in its compat
	//      tag (the reverse direction). For our purpose -- letting a PPABP compiled against
	//      the source skeleton evaluate on a mesh whose skeleton is TargetSkeleton -- only
	//      the one-way membership Source IN TargetSkeleton->CompatibleSkeletons matters.
	//
	// So consult only TargetSkeleton's own CompatibleSkeletons list.
	TSet<FSoftObjectPath> AlreadyCompatible;
	for (const TSoftObjectPtr<USkeleton>& Existing : TargetSkeleton->GetCompatibleSkeletons())
	{
		AlreadyCompatible.Add(Existing.ToSoftObjectPath());
	}

	TArray<USkeleton*> SkeletonsToAdd;
	SkeletonsToAdd.Reserve(SkeletonsToConsider.Num());

	for (USkeleton* Candidate : SkeletonsToConsider)
	{
		if (!Candidate || Candidate == TargetSkeleton)
		{
			continue;
		}

		if (SkeletonsToAdd.Contains(Candidate))
		{
			continue;
		}

		if (AlreadyCompatible.Contains(FSoftObjectPath(Candidate)))
		{
			continue;
		}

		SkeletonsToAdd.Add(Candidate);
	}

	if (SkeletonsToAdd.IsEmpty())
	{
		// Nothing to do -- treat as success so the caller doesn't surface a spurious failure.
		return true;
	}

	if (FApp::IsUnattended())
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error,
			"Target Skeleton {Target} is missing {Count} compatible skeleton(s) required by "
			"meshes in this Collection: {Collection}. "
			"Rebuild the Collection manually to have this automatically fixed.",
			TargetSkeleton->GetPathName(), SkeletonsToAdd.Num(), Collection->GetPathName());

		return false;
	}

	// Build a bullet list of the skeletons we're proposing to add, by full asset path so the
	// user can identify them unambiguously.
	FString SkeletonList;
	for (const USkeleton* Skeleton : SkeletonsToAdd)
	{
		SkeletonList += FString::Printf(TEXT("\n    \u2022 %s"), *Skeleton->GetPathName());
	}

	const FText DialogTitle = LOCTEXT("AddCompatibleSkeletonsTitle", "Mark Skeletons As Compatible?");
	const FText DialogMessage = FText::Format(
		LOCTEXT("AddCompatibleSkeletonsMessage",
			"The following skeleton(s) are referenced by meshes in this Collection but are not yet in the "
			"Target Skeleton's compatibility list:{1}\n\n"
			"Add them now? (This modifies the Target Skeleton asset)"),
		FText::FromString(TargetSkeleton->GetPathName()),
		FText::FromString(SkeletonList));

	enum
	{
		Button_AddAll = 0,
		Button_Cancel = 1,
	};

	const TSharedRef<SCustomDialog> Dialog = SNew(SCustomDialog)
		.Title(DialogTitle)
		.Content()
		[
			SNew(STextBlock)
				.Text(DialogMessage)
				.AutoWrapText(true)
		]
		.Buttons({
			SCustomDialog::FButton(LOCTEXT("AddAllCompatibleSkeletonsButton", "Mark as compatible"))
				.SetButtonRole(SCustomDialog::EButtonRole::Confirm),
			SCustomDialog::FButton(LOCTEXT("CancelButton", "Cancel"))
				.SetButtonRole(SCustomDialog::EButtonRole::Cancel)
		});

	const int32 Choice = Dialog->ShowModal(Button_Cancel);

	if (Choice != Button_AddAll)
	{
		return false;
	}

	for (USkeleton* Skeleton : SkeletonsToAdd)
	{
		TargetSkeleton->AddCompatibleSkeleton(Skeleton);
	}
	TargetSkeleton->MarkPackageDirty();

	UE_LOGFMT(LogMetaHumanCrowdEditor, Log,
		"Added {Count} compatible skeleton(s) to {Target} from user prompt.",
		SkeletonsToAdd.Num(), TargetSkeleton->GetPathName());

	return true;
}

} // namespace UE::MetaHuman::CrowdEditor

#undef LOCTEXT_NAMESPACE
