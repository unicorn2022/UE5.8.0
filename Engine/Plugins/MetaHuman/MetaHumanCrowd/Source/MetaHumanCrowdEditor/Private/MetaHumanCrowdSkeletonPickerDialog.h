// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMetaHumanCollection;
class USkeleton;

namespace UE::MetaHuman::CrowdEditor
{
	/**
	 * Prompt the user to either select an existing skeleton or create a new one duplicated from
	 * the given template skeleton.
	 *
	 * @param TemplateSkeleton  The template to duplicate when the user chooses "create new"
	 * @param DefaultCreatePath Optional long package path used as the default location in the "create new"
	 *                          save asset dialog
	 * @param OutSkeleton       On success, set to the chosen / created skeleton.
	 * @return true if the user chose or created a satisfactory skeleton.
	 */
	bool PromptForTargetSkeleton(USkeleton* TemplateSkeleton, const FString& DefaultCreatePath, USkeleton*& OutSkeleton);

	/**
	 * Prompt the user to add a list of skeletons to TargetSkeleton's CompatibleSkeletons list.
	 *
	 * @param Collection          The Collection being built, for logging purposes.
	 * @param TargetSkeleton      The skeleton being prepared for use by the pipeline.
	 * @param SkeletonsToConsider Candidate skeletons to add. Duplicates and nulls are tolerated.
	 * @return true if the user agreed (or there was nothing to do); false if the user cancelled.
	 */
	bool PromptToAddCompatibleSkeletons(
		TNotNull<const UMetaHumanCollection*> Collection,
		USkeleton* TargetSkeleton,
		const TArray<USkeleton*>& SkeletonsToConsider);
}
