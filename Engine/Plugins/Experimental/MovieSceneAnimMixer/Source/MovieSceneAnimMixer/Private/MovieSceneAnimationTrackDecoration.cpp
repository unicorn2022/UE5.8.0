// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneAnimationTrackDecoration.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntityBuilder.h"
#include "AnimMixerComponentTypes.h"
#include "MovieSceneAnimationMixerTrack.h"
#include "MovieSceneTracksComponentTypes.h"
#include "MovieSceneAnimationMixerLayer.h"

void UMovieSceneAnimationTrackDecoration::ExtendEntityImpl(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity)
{
	using namespace UE::MovieScene;
	FAnimMixerComponentTypes* AnimMixerComponents = FAnimMixerComponentTypes::Get();

	UMovieSceneTrack* ChildTrack = GetTypedOuter<UMovieSceneTrack>();

	UMovieSceneAnimationMixerLayer* MixerLayer = MixerTrack ? MixerTrack->GetLayerForChildTrack(ChildTrack) : nullptr;

	TInstancedStruct<FMovieSceneMixedAnimationTarget> Target;
	if (MixerTrack != nullptr && MixerTrack->MixedAnimationTarget.IsValid())
	{
		Target = MixerTrack->MixedAnimationTarget;
	}
	else
	{
		Target = TInstancedStruct<FMovieSceneMixedAnimationTarget>::Make();
	}

	OutImportedEntity->AddBuilder(
		FEntityBuilder()
		.AddTag(FMovieSceneTracksComponentTypes::Get()->Tags.AnimMixerPoseProducer)
		.Add(AnimMixerComponents->Target, Target)
		.Add(AnimMixerComponents->Priority, MixerLayer ? MixerLayer->GetLayerIndex() : 0)
		.Add(AnimMixerComponents->MixerLayer, MixerLayer)
		.AddMutualComponents()
	);
}
