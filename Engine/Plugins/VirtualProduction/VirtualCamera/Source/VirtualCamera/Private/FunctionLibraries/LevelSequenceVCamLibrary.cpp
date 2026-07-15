// Copyright Epic Games, Inc. All Rights Reserved.

#include "FunctionLibraries/LevelSequenceVCamLibrary.h"
#include "VirtualCamera.h"

#include "Camera/CameraComponent.h"
#include "Constraint.h"
#include "GameFramework/Actor.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Transform/TransformConstraint.h"
#include "EngineUtils.h"
#include "MovieSceneSpawnableAnnotation.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "Compilation/MovieSceneCompiledDataManager.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "Evaluation/MovieSceneSequenceHierarchy.h"

#if WITH_EDITOR
#include "ILevelSequenceEditorToolkit.h"
#include "ISequencer.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#endif

namespace UE::VirtualCamera::Private
{
#if WITH_EDITOR
	static TSharedPtr<ISequencer> GetGlobalSequencer()
	{
		// if getting sequencer from level sequence need to use the current(leader), not the focused
		if (ULevelSequence* LevelSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence())
		{
			if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				static constexpr bool bFocusIfOpen = false;
				IAssetEditorInstance* AssetEditor = AssetEditorSubsystem->FindEditorForAsset(LevelSequence, bFocusIfOpen);
				const ILevelSequenceEditorToolkit* LevelSequenceEditor = static_cast<ILevelSequenceEditorToolkit*>(AssetEditor);
				return LevelSequenceEditor ? LevelSequenceEditor->GetSequencer() : nullptr;
			}
		}
		return nullptr;
	}

	static void IterateActorsAffectedBySequence(ISequencer& Sequencer, UMovieSceneSequence* Sequence, FMovieSceneSequenceIDRef SequenceID, const FMovieSceneSequenceHierarchy* Hierarchy, TFunctionRef<void(AActor&)> OnActorFound)
	{
		if (UMovieScene* MovieScene = Sequence->GetMovieScene())
		{
			// Search all possessables
			for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); ++Index)
			{
				FGuid ThisGuid = MovieScene->GetPossessable(Index).GetGuid();

				for (TWeakObjectPtr<> WeakObject : Sequencer.FindBoundObjects(ThisGuid, SequenceID))
				{
					AActor* Actor = Cast<AActor>(WeakObject.Get());
					if (Actor != nullptr)
					{
						OnActorFound(*Actor);
					}
				}
			}

			// Search all spawnables
			for (int32 Index = 0; Index < MovieScene->GetSpawnableCount(); ++Index)
			{
				FGuid ThisGuid = MovieScene->GetSpawnable(Index).GetGuid();

				for (TWeakObjectPtr<> WeakObject : Sequencer.FindBoundObjects(ThisGuid, SequenceID))
				{
					AActor* Actor = Cast<AActor>(WeakObject.Get());
					if (Actor != nullptr)
					{
						OnActorFound(*Actor);
					}
				}
			}
		}

		if (Hierarchy)
		{
			// Recurse into child nodes
			if (const FMovieSceneSequenceHierarchyNode* Node = Hierarchy->FindNode(SequenceID))
			{
				for (FMovieSceneSequenceIDRef ChildID : Node->Children)
				{
					const FMovieSceneSubSequenceData* SubData     = Hierarchy->FindSubData(ChildID);
					UMovieSceneSequence*              SubSequence = SubData ? SubData->GetSequence() : nullptr;

					if (SubSequence)
					{
						IterateActorsAffectedBySequence(Sequencer, SubSequence, ChildID, Hierarchy, OnActorFound);
					}
				}
			}
		}
	}
#endif
}

// ---------------------------------------------------------------------------
// ClearVCamConstraintBakeTracks helpers
// ---------------------------------------------------------------------------

#if WITH_EDITOR

static EMovieSceneTransformChannel GetDrivenChannelMask(const UTickableParentConstraint* Constraint)
{
	const FTransformFilter& Filter = Constraint->TransformFilter;
	EMovieSceneTransformChannel Mask = EMovieSceneTransformChannel::None;

	if (Filter.TranslationFilter.bX) { EnumAddFlags(Mask, EMovieSceneTransformChannel::TranslationX); }
	if (Filter.TranslationFilter.bY) { EnumAddFlags(Mask, EMovieSceneTransformChannel::TranslationY); }
	if (Filter.TranslationFilter.bZ) { EnumAddFlags(Mask, EMovieSceneTransformChannel::TranslationZ); }
	if (Filter.RotationFilter.bX)    { EnumAddFlags(Mask, EMovieSceneTransformChannel::RotationX); }
	if (Filter.RotationFilter.bY)    { EnumAddFlags(Mask, EMovieSceneTransformChannel::RotationY); }
	if (Filter.RotationFilter.bZ)    { EnumAddFlags(Mask, EMovieSceneTransformChannel::RotationZ); }
	if (Filter.ScaleFilter.bX)       { EnumAddFlags(Mask, EMovieSceneTransformChannel::ScaleX); }
	if (Filter.ScaleFilter.bY)       { EnumAddFlags(Mask, EMovieSceneTransformChannel::ScaleY); }
	if (Filter.ScaleFilter.bZ)       { EnumAddFlags(Mask, EMovieSceneTransformChannel::ScaleZ); }

	return Mask;
}

static void ClearUndrivenChannelKeys(UMovieScene3DTransformSection* Section, EMovieSceneTransformChannel DrivenMask)
{
	static const EMovieSceneTransformChannel ChannelFlags[9] = {
		EMovieSceneTransformChannel::TranslationX,
		EMovieSceneTransformChannel::TranslationY,
		EMovieSceneTransformChannel::TranslationZ,
		EMovieSceneTransformChannel::RotationX,
		EMovieSceneTransformChannel::RotationY,
		EMovieSceneTransformChannel::RotationZ,
		EMovieSceneTransformChannel::ScaleX,
		EMovieSceneTransformChannel::ScaleY,
		EMovieSceneTransformChannel::ScaleZ,
	};

	TArrayView<FMovieSceneDoubleChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
	check(Channels.Num() >= 9);

	for (int32 ChannelIndex = 0; ChannelIndex < 9; ++ChannelIndex)
	{
		if (EnumHasAnyFlags(DrivenMask, ChannelFlags[ChannelIndex]))
		{
			continue; // constraint drives this channel — leave it alone
		}

		FMovieSceneDoubleChannel* Channel = Channels[ChannelIndex];
		if (!Channel)
		{
			continue;
		}

		TArray<FFrameNumber> Times;
		TArray<FKeyHandle> Handles;
		Channel->GetKeys(TRange<FFrameNumber>::All(), &Times, &Handles);
		Channel->DeleteKeys(Handles);

		const double IdentityValue = (ChannelIndex >= 6) ? 1.0 : 0.0; // scale channels default to 1
		Channel->SetDefault(IdentityValue);
	}
}

#endif // WITH_EDITOR

// ---------------------------------------------------------------------------

int32 ULevelSequenceVCamLibrary::ClearVCamConstraintBakeTracks(ULevelSequence* Sequence, AActor* VCamActor)
{
#if WITH_EDITOR
	if (!Sequence)
	{
		UE_LOG(LogVirtualCamera, Warning, TEXT("ClearVCamConstraintBakeTracks: Sequence is null."));
		return 0;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return 0;
	}

	// Resolve the actor label once up front for matching against binding names.
	// Binding names in Sequencer match GetActorLabel() in editor.
	const FString ActorLabel = VCamActor ? VCamActor->GetActorLabel() : FString();

	int32 SectionsCleared = 0;

	for (const FMovieSceneBinding& Binding : static_cast<const UMovieScene*>(MovieScene)->GetBindings())
	{
		FString BindingName;
		if (const FMovieScenePossessable* Possessable = MovieScene->FindPossessable(Binding.GetObjectGuid()))
		{
			BindingName = Possessable->GetName();
		}
		else if (const FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(Binding.GetObjectGuid()))
		{
			BindingName = Spawnable->GetName();
		}

		if (!ActorLabel.IsEmpty() && BindingName != ActorLabel)
		{
			continue;
		}

		for (UMovieSceneTrack* Track : Binding.GetTracks())
		{
			UMovieScene3DTransformTrack* TransformTrack = Cast<UMovieScene3DTransformTrack>(Track);
			if (!TransformTrack)
			{
				continue;
			}

			for (UMovieSceneSection* Section : TransformTrack->GetAllSections())
			{
				UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(Section);
				if (!TransformSection)
				{
					continue;
				}

				IMovieSceneConstrainedSection* ConstrainedSection = Cast<IMovieSceneConstrainedSection>(TransformSection);
				if (!ConstrainedSection || ConstrainedSection->GetConstraintsChannels().IsEmpty())
				{
					continue;
				}

				UTickableParentConstraint* ParentConstraint = nullptr;
				for (const FConstraintAndActiveChannel& Ch : ConstrainedSection->GetConstraintsChannels())
				{
					ParentConstraint = Cast<UTickableParentConstraint>(Ch.GetConstraint());
					if (ParentConstraint)
					{
						break;
					}
				}

				TransformSection->Modify();

				if (ParentConstraint)
				{
					const EMovieSceneTransformChannel DrivenMask = GetDrivenChannelMask(ParentConstraint);
					TransformSection->SetMask(FMovieSceneTransformMask{ DrivenMask });
					ClearUndrivenChannelKeys(TransformSection, DrivenMask);
					UE_LOG(LogVirtualCamera, Verbose, TEXT("ClearVCamConstraintBakeTracks: Set mask 0x%X on '%s'"), (uint32)DrivenMask, *BindingName);
				}
				else
				{
					TransformSection->SetMask(FMovieSceneTransformMask{ EMovieSceneTransformChannel::None });
					UE_LOG(LogVirtualCamera, Verbose, TEXT("ClearVCamConstraintBakeTracks: Set mask None on '%s'"), *BindingName);
				}

				++SectionsCleared;
			}
		}
	}

	if (SectionsCleared == 0)
	{
		UE_LOG(LogVirtualCamera, Warning, TEXT("ClearVCamConstraintBakeTracks: No baked transform sections found."));
	}

	return SectionsCleared;
#else
	return 0;
#endif
}

// ---------------------------------------------------------------------------

bool ULevelSequenceVCamLibrary::HasAnyCameraCutsInLevelSequence(ULevelSequence* Sequence)
{
	if (!Sequence)
	{
		return false;
	}
	
	UMovieScene* FocusedScene = Sequence->GetMovieScene();
	UMovieSceneTrack* CameraCutTrack = FocusedScene->GetCameraCutTrack();
	return CameraCutTrack && !CameraCutTrack->IsEmpty();
}

TArray<FPilotableSequenceCameraInfo> ULevelSequenceVCamLibrary::FindPilotableCamerasInActiveLevelSequence()
{
	TArray<FPilotableSequenceCameraInfo> Result;
#if WITH_EDITOR
	if (const TSharedPtr<ISequencer> Sequencer = UE::VirtualCamera::Private::GetGlobalSequencer())
	{
		FMovieSceneRootEvaluationTemplateInstance& RootInstance = Sequencer->GetEvaluationTemplate();
		const FMovieSceneSequenceHierarchy*        Hierarchy    = RootInstance.GetCompiledDataManager()->FindHierarchy(RootInstance.GetCompiledDataID());

		UMovieSceneSequence* RootSequence = Sequencer->GetRootMovieSceneSequence();
		UE::VirtualCamera::Private::IterateActorsAffectedBySequence(*Sequencer,
			RootSequence,
			MovieSceneSequenceID::Root,
			Hierarchy,
			[&Result](AActor& Actor)
			{
				if (UCameraComponent* Camera = Actor.FindComponentByClass<UCameraComponent>())
				{
					Result.Add({ Camera });
				}
			});
	}
#endif
	return Result;
}
