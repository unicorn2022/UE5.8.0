// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/ArrayView.h"
#include "Containers/Set.h"
#include "Templates/Function.h"
#include "UObject/NameTypes.h"

namespace UE::MetaHuman::DependencyWalker
{
	enum class EVisitResult : uint8
	{
		Follow,  // Continue BFS through this dep's own dependencies.
		Skip,    // Do not descend. Dep is still marked seen and won't be revisited.
	};

	/**
	 * Visitor signature: (Source, Dependency) -> EVisitResult. Seed packages are
	 * never passed to the visitor — they are roots, not discovered deps.
	 */
	using FVisitorFn = TFunctionRef<EVisitResult(const FName& Source, const FName& Dependency)>;

	/**
	 * BFS over package dependencies via IAssetRegistry::GetDependencies starting from
	 * Seeds. Each newly-discovered dep is passed once to Visitor; already-seen deps
	 * are not revisited and the visitor cannot "undo" a previous Follow.
	 */
	void WalkDependencies(TArrayView<const FName> Seeds, FVisitorFn Visitor);
}
