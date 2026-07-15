// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProjectUtilities/MetaHumanDependencyWalker.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Containers/Queue.h"

namespace UE::MetaHuman::DependencyWalker
{
	void WalkDependencies(TArrayView<const FName> Seeds, FVisitorFn Visitor)
	{
		TSet<FName> Seen;
		Seen.Reserve(Seeds.Num());

		TQueue<FName> ToProcess;
		for (const FName& Seed : Seeds)
		{
			bool bAlreadySeen = false;
			Seen.Add(Seed, &bAlreadySeen);
			if (!bAlreadySeen)
			{
				ToProcess.Enqueue(Seed);
			}
		}

		const IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
		FName Source;
		while (ToProcess.Dequeue(Source))
		{
			TArray<FName> Dependencies;
			AssetRegistry.GetDependencies(Source, Dependencies);
			for (const FName& Dependency : Dependencies)
			{
				bool bAlreadySeen = false;
				Seen.Add(Dependency, &bAlreadySeen);
				if (bAlreadySeen)
				{
					continue;
				}

				const EVisitResult Result = Visitor(Source, Dependency);
				if (Result == EVisitResult::Follow)
				{
					ToProcess.Enqueue(Dependency);
				}
			}
		}
	}
}
