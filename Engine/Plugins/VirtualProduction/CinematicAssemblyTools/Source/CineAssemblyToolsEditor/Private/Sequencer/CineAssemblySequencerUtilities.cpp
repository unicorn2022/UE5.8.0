// Copyright Epic Games, Inc. All Rights Reserved.

#include "CineAssemblySequencerUtilities.h"

#include "Bindings/MovieSceneCustomBinding.h"
#include "Channels/MovieSceneActorReferenceChannel.h"
#include "CineCameraActor.h"
#include "Editor.h"
#include "ISequencer.h"
#include "LevelSequenceEditorSubsystem.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneBindingProxy.h"
#include "MovieSceneBindingReferences.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSequence.h"
#include "MovieSceneSpawnable.h"
#include "ScopedTransaction.h"
#include "Sections/MovieScene3DConstraintSection.h"
#include "Sections/MovieSceneActorReferenceSection.h"
#include "Sections/MovieSceneAudioSection.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieSceneSubSection.h"
#include "SequencerUtilities.h"

#define LOCTEXT_NAMESPACE "CineAssemblySequencerUtilities"

namespace UE::CineAssemblyTools::Private
{
	/** Deep-duplicate every root track (including the camera cut track) from InSrcSequence's MovieScene into InDstSequence's MovieScene. */
	void DuplicateTracks(UMovieSceneSequence* InSrcSequence, UMovieSceneSequence* InDstSequence, TArray<UMovieSceneTrack*>& OutPastedTracks)
	{
		UMovieScene* SrcMovieScene = InSrcSequence->GetMovieScene();
		UMovieScene* DstMovieScene = InDstSequence->GetMovieScene();
		if (!SrcMovieScene || !DstMovieScene)
		{
			return;
		}

		// Duplicate every track from the source movie scene into the destination movie scene.
		for (UMovieSceneTrack* SrcTrack : SrcMovieScene->GetTracks())
		{
			if (SrcTrack)
			{
				UMovieSceneTrack* DstTrack = Cast<UMovieSceneTrack>(StaticDuplicateObject(SrcTrack, DstMovieScene));
				if (DstTrack && DstMovieScene->AddGivenTrack(DstTrack))
				{
					OutPastedTracks.Add(DstTrack);
				}
			}
		}

		// Duplicate the camera cut track (if it exists)
		if (UMovieSceneTrack* SrcCameraCutTrack = SrcMovieScene->GetCameraCutTrack())
		{
			if (UMovieSceneTrack* DstCameraCutTrack = Cast<UMovieSceneTrack>(StaticDuplicateObject(SrcCameraCutTrack, DstMovieScene)))
			{
				DstMovieScene->SetCameraCutTrack(DstCameraCutTrack);
				OutPastedTracks.Add(DstCameraCutTrack);
			}
		}
	}

	/**
	 * Duplicate a legacy FMovieSceneSpawnable into InDstMovieScene with a fresh GUID. Returns an invalid FGuid on failure.
	 */
	FGuid DuplicateLegacySpawnable(const FMovieSceneSpawnable& InSrcSpawnable, UMovieScene& InDstMovieScene)
	{
		// Duplicate the object template into the destination so the saved package doesn't reference a privately-owned template inside the source.
		const UObject* SrcTemplateObj = InSrcSpawnable.GetObjectTemplate();
		UObject* DstTemplateObj = SrcTemplateObj ? StaticDuplicateObject(SrcTemplateObj, &InDstMovieScene) : nullptr;
		if (!DstTemplateObj)
		{
			return FGuid();
		}

		const FGuid DstGuid = InDstMovieScene.AddSpawnable(InSrcSpawnable.GetName(), *DstTemplateObj);

		// Carry over the spawnable's auxiliary fields (ownership policy, respawn behavior, etc.).
		if (FMovieSceneSpawnable* DstSpawnable = InDstMovieScene.FindSpawnable(DstGuid))
		{
			DstSpawnable->SetSpawnOwnership(InSrcSpawnable.GetSpawnOwnership());
			DstSpawnable->bContinuouslyRespawn = InSrcSpawnable.bContinuouslyRespawn;
			DstSpawnable->bNetAddressableName  = InSrcSpawnable.bNetAddressableName;
		}

		return DstGuid;
	}

	/** Recreate every binding from InSrcSequence's MovieScene on InDstSequence's MovieScene with a fresh GUID, and emit the source-to-destination GUID mapping. */
	void DuplicateBindings(UMovieSceneSequence* InSrcSequence, UMovieSceneSequence* InDstSequence, TArray<FMovieSceneBindingProxy>& OutPastedBindings, TMap<FGuid, FGuid>& OutSrcToDstBindingMap)
	{
		OutSrcToDstBindingMap.Reset();

		UMovieScene* SrcMovieScene = InSrcSequence->GetMovieScene();
		UMovieScene* DstMovieScene = InDstSequence->GetMovieScene();
		if (!SrcMovieScene || !DstMovieScene)
		{
			return;
		}

		const FMovieSceneBindingReferences* SrcBindingRefs = InSrcSequence->GetBindingReferences();
		FMovieSceneBindingReferences* DstBindingRefs = InDstSequence->GetBindingReferences();

		for (const FMovieSceneBinding& SrcBinding : AsConst(*SrcMovieScene).GetBindings())
		{
			const FGuid SrcGuid = SrcBinding.GetObjectGuid();
			FGuid DstGuid;

			// Add a duplicate binding to the DstMovieScene
			if (const FMovieScenePossessable* SrcPossessable = SrcMovieScene->FindPossessable(SrcGuid))
			{
				UClass* SrcClass = const_cast<UClass*>(SrcPossessable->GetPossessedObjectClass());
				DstGuid = DstMovieScene->AddPossessable(SrcPossessable->GetName(), SrcClass);
			}
			else if (const FMovieSceneSpawnable* SrcSpawnable = SrcMovieScene->FindSpawnable(SrcGuid))
			{
				DstGuid = DuplicateLegacySpawnable(*SrcSpawnable, *DstMovieScene);
			}
			else
			{
				continue;
			}

			if (!DstGuid.IsValid())
			{
				continue;
			}

			OutSrcToDstBindingMap.Add(SrcGuid, DstGuid);

			// Duplicate each track under the source binding into the destination's MovieScene under the fresh GUID.
			for (UMovieSceneTrack* SrcTrack : SrcBinding.GetTracks())
			{
				if (SrcTrack)
				{
					if (UMovieSceneTrack* DstTrack = Cast<UMovieSceneTrack>(StaticDuplicateObject(SrcTrack, DstMovieScene)))
					{
						DstMovieScene->AddGivenTrack(DstTrack, DstGuid);
					}
				}
			}

			// Copy custom binding references
			if (SrcBindingRefs && DstBindingRefs)
			{
				for (const FMovieSceneBindingReference& SrcRef : SrcBindingRefs->GetReferences(SrcGuid))
				{
					// Schema TemplateSequences break all possessable bindings, so we are only concerned about binding references with a valid CustomBinding
					if (SrcRef.CustomBinding)
					{
						UMovieSceneCustomBinding* DstCustom = Cast<UMovieSceneCustomBinding>(StaticDuplicateObject(SrcRef.CustomBinding.Get(), DstMovieScene));
						if (DstCustom)
						{
							DstBindingRefs->AddBinding(DstGuid, DstCustom);
						}
					}
				}
			}

			OutPastedBindings.Emplace(DstGuid, InDstSequence);
		}

		// Reparent possessables whose parent bindings were duplicated so they reference the correct duplicate binding ID
		for (const TPair<FGuid, FGuid>& GuidPair : OutSrcToDstBindingMap)
		{
			const FGuid& SrcGuid = GuidPair.Key;
			const FGuid& DstGuid = GuidPair.Value;

			const FMovieScenePossessable* SrcPossessable = SrcMovieScene->FindPossessable(SrcGuid);
			if (!SrcPossessable)
			{
				continue;
			}

			const FGuid SrcParentGuid = SrcPossessable->GetParent();
			const FGuid* DstParentGuidPtr = OutSrcToDstBindingMap.Find(SrcParentGuid);
			if (!DstParentGuidPtr)
			{
				continue;
			}

			FMovieScenePossessable* DstPossessable = DstMovieScene->FindPossessable(DstGuid);
			if (DstPossessable && DstGuid != *DstParentGuidPtr)
			{
				DstPossessable->SetParent(*DstParentGuidPtr, DstMovieScene);
			}
		}
	}

	/** Walk an FMovieSceneActorReferenceChannel's default value and keys, rewriting any FMovieSceneObjectBindingID whose source GUID is in the map. */
	bool RemapActorReferenceChannel(const TMap<FGuid, FGuid>& InSrcToDstBindingMap, FMovieSceneActorReferenceChannel& InChannel)
	{
		bool bModified = false;

		// Reassign the default actor ref GUID
		FMovieSceneActorReferenceKey DefaultKey = InChannel.GetDefault();
		if (const FGuid* NewGuid = InSrcToDstBindingMap.Find(DefaultKey.Object.GetGuid()))
		{
			DefaultKey.Object = UE::MovieScene::FRelativeObjectBindingID(*NewGuid);
			InChannel.SetDefault(DefaultKey);
			bModified = true;
		}

		// Reassign any keyed actor ref GUIDs
		for (FMovieSceneActorReferenceKey& Key : InChannel.GetData().GetValues())
		{
			if (const FGuid* NewGuid = InSrcToDstBindingMap.Find(Key.Object.GetGuid()))
			{
				Key.Object = UE::MovieScene::FRelativeObjectBindingID(*NewGuid);
				bModified = true;
			}
		}

		return bModified;
	}

	/** Reassign binding references in the pasted content using the new pasted Binding IDs */
	void ReassignBindingIDs(const TMap<FGuid, FGuid>& InSrcToDstBindingMap, TConstArrayView<UMovieSceneTrack*> InPastedTracks)
	{
		if (InSrcToDstBindingMap.IsEmpty())
		{
			return;
		}

		for (UMovieSceneTrack* Track : InPastedTracks)
		{
			if (!Track)
			{
				continue;
			}

			for (UMovieSceneSection* Section : Track->GetAllSections())
			{
				if (UMovieSceneCameraCutSection* CameraCutSection = Cast<UMovieSceneCameraCutSection>(Section))
				{
					const FMovieSceneObjectBindingID& CurrentID = CameraCutSection->GetCameraBindingID();
					if (CurrentID.IsValid())
					{
						if (const FGuid* NewGuid = InSrcToDstBindingMap.Find(CurrentID.GetGuid()))
						{
							CameraCutSection->Modify();
							CameraCutSection->SetCameraBindingID(UE::MovieScene::FRelativeObjectBindingID(*NewGuid));
						}
					}
				}
				else if (UMovieScene3DConstraintSection* ConstraintSection = Cast<UMovieScene3DConstraintSection>(Section))
				{
					// Covers 3DAttach, Path, and Look-At sections
					const FMovieSceneObjectBindingID& CurrentID = ConstraintSection->GetConstraintBindingID();
					if (CurrentID.IsValid())
					{
						if (const FGuid* NewGuid = InSrcToDstBindingMap.Find(CurrentID.GetGuid()))
						{
							ConstraintSection->Modify();
							ConstraintSection->SetConstraintBindingID(UE::MovieScene::FRelativeObjectBindingID(*NewGuid));
						}
					}
				}
				else if (UMovieSceneAudioSection* AudioSection = Cast<UMovieSceneAudioSection>(Section))
				{
					FMovieSceneActorReferenceChannel& ActorRefChannel = const_cast<FMovieSceneActorReferenceChannel&>(AudioSection->GetAttachActorData());
					if (RemapActorReferenceChannel(InSrcToDstBindingMap, ActorRefChannel))
					{
						AudioSection->Modify();
					}
				}
				else if (UMovieSceneActorReferenceSection* ActorRefSection = Cast<UMovieSceneActorReferenceSection>(Section))
				{
					FMovieSceneActorReferenceChannel& ActorRefChannel = const_cast<FMovieSceneActorReferenceChannel&>(ActorRefSection->GetActorReferenceData());
					if (RemapActorReferenceChannel(InSrcToDstBindingMap, ActorRefChannel))
					{
						ActorRefSection->Modify();
					}
				}
			}
		}
	}
}

void FCineAssemblySequencerUtilities::CreateCamera(TSharedRef<ISequencer> Sequencer)
{
	const FScopedTransaction Transaction(LOCTEXT("CreateCameraTransaction", "Create Camera"));

	// Create the new cine camera actor, but do not spawn it in the world
	ACineCameraActor* NewCamera = NewObject<ACineCameraActor>();

	// Add a new spawnable binding to the sequence for the new cine camera
	UE::Sequencer::FCreateBindingParams Params;
	Params.bAllowCustomBinding = true;
	Params.bSpawnable = true;
	Params.bSetupDefaults = true;
	Params.BindingNameOverride = TEXT("CineCameraActor");

	const FGuid NewCameraID = FSequencerUtilities::CreateBinding(Sequencer, *NewCamera, Params);

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
	if (NewCameraID.IsValid() && MovieScene)
	{
		if (ULevelSequenceEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<ULevelSequenceEditorSubsystem>())
		{
			Subsystem->AddDefaultTracksForActor(Sequencer, *NewCamera, NewCameraID);
		}

		// Because our spawn register never actually spawns, AddDefaultTracksForActor ends up creating a
		// duplicate top-level camera binding (with the CameraComponent and its property tracks under it)
		// instead of attaching them to our spawnable. Reparent its children onto NewCameraID and remove it.
		constexpr bool bCreateHandleIfMissing = false;
		const FGuid DuplicateCameraID = Sequencer->GetHandleToObject(NewCamera, bCreateHandleIfMissing);
		if (DuplicateCameraID.IsValid())
		{
			for (int32 PossessableIndex = 0; PossessableIndex < MovieScene->GetPossessableCount(); ++PossessableIndex)
			{
				FMovieScenePossessable& Possessable = MovieScene->GetPossessable(PossessableIndex);
				if (Possessable.GetParent() == DuplicateCameraID)
				{
					Possessable.SetParent(NewCameraID, MovieScene);
				}
			}

			MovieScene->RemovePossessable(DuplicateCameraID);
		}

		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
	}
}

void FCineAssemblySequencerUtilities::ReplaceSubSequence(UMovieSceneSubSection* InSubSection, UMovieSceneSequence* InSequence)
{
	if (InSubSection)
	{
		const bool bIsLocked = InSubSection->IsLocked();
		InSubSection->SetIsLocked(false);
		InSubSection->SetSequence(InSequence);
		InSubSection->SetIsLocked(bIsLocked);
	}
}

void FCineAssemblySequencerUtilities::DuplicateMovieSceneContents(UMovieSceneSequence* InSrcSequence, UMovieSceneSequence* InDstSequence)
{
	using namespace UE::CineAssemblyTools::Private;

	if (!InSrcSequence || !InDstSequence)
	{
		return;
	}

	TArray<UMovieSceneTrack*> PastedTracks;
	DuplicateTracks(InSrcSequence, InDstSequence, PastedTracks);

	TArray<FMovieSceneBindingProxy> PastedBindings;
	TMap<FGuid, FGuid> SrcToDstBindingMap;
	DuplicateBindings(InSrcSequence, InDstSequence, PastedBindings, SrcToDstBindingMap);

	// Append tracks under each pasted binding so the reassignment below covers both root and binding tracks.
	if (UMovieScene* TargetMovieScene = InDstSequence->GetMovieScene())
	{
		for (const FMovieSceneBindingProxy& BindingProxy : PastedBindings)
		{
			if (const FMovieSceneBinding* Binding = TargetMovieScene->FindBinding(BindingProxy.BindingID))
			{
				PastedTracks.Append(Binding->GetTracks());
			}
		}
	}

	ReassignBindingIDs(SrcToDstBindingMap, PastedTracks);
}

#undef LOCTEXT_NAMESPACE
