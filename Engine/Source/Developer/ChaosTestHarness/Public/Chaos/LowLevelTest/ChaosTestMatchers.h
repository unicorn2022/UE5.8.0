// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/LowLevelTest/ChaosTestCatch2.h"
#include <sstream>

namespace Catch
{
	template <typename T, int D>
	class FVectorEqualityMatcher : public Catch::Matchers::MatcherBase<Chaos::TVector<T, D>>
	{
	public:
		using FVector = Chaos::TVector<T, D>;
		FVector Target;
		T Epsilon;

		FVectorEqualityMatcher(const FVector& InTarget, const T InEpsilon = UE_KINDA_SMALL_NUMBER)
			: Target(InTarget)
			, Epsilon(InEpsilon)
		{
		}

		// Performs the test for this matcher
		bool match(const FVector& ToCompare) const override
		{
			return FVector::PointsAreNear(ToCompare, Target, Epsilon);
		}

		// Produces a string describing what this matcher does. It should
		// include any provided data (the begin/ end in this case) and
		// be written as if it were stating a fact (in the output it will be
		// preceded by the value under test).
		virtual std::string describe() const override
		{
			std::ostringstream Stream;
			Stream << "is equal to (" << Catch::StringMaker<FVector>::convert(Target) << ") with epsilon " << Epsilon;
			return Stream.str();
		}
	};

	template <typename T, int D>
	FVectorEqualityMatcher<T, D> ApproxEq(const Chaos::TVector<T, D>& Target, const T Epsilon = UE_KINDA_SMALL_NUMBER)
	{
		return FVectorEqualityMatcher<T, D>(Target, Epsilon);
	}

	template <typename T>
	FVectorEqualityMatcher<T, 3> ApproxEq(const UE::Math::TVector<T>& Target, const T Epsilon = UE_KINDA_SMALL_NUMBER)
	{
		return ApproxEq(Chaos::TVec3<T>(Target), Epsilon);
	}

	template <typename T>
	class FQuatEqualityMatcher : public Catch::Matchers::MatcherBase<UE::Math::TQuat<T>>
	{
	public:
		using FQuat = UE::Math::TQuat<T>;
		FQuat Target;
		T Epsilon;

		FQuatEqualityMatcher(const FQuat& InTarget, const T InEpsilon = UE_KINDA_SMALL_NUMBER)
			: Target(InTarget)
			, Epsilon(InEpsilon)
		{
		}

		// Performs the test for this matcher
		bool match(const FQuat& ToCompare) const override
		{
			return Target.Equals(ToCompare, Epsilon);
		}

		// Produces a string describing what this matcher does. It should
		// include any provided data (the begin/ end in this case) and
		// be written as if it were stating a fact (in the output it will be
		// preceded by the value under test).
		virtual std::string describe() const override
		{
			std::ostringstream Stream;
			Stream << "is equal to (" << Catch::StringMaker<FQuat>::convert(Target) << ") with epsilon " << Epsilon;
			return Stream.str();
		}
	};

	template <typename T>
	FQuatEqualityMatcher<T> ApproxEq(const UE::Math::TQuat<T>& Target, const T Epsilon = UE_KINDA_SMALL_NUMBER)
	{
		return FQuatEqualityMatcher<T>(Target, Epsilon);
	}

	template <typename T, int D>
	class TAABBEqualityMatcher : public Catch::Matchers::MatcherBase<Chaos::TAABB<T, D>>
	{
	public:
		using FAABB = Chaos::TAABB<T, D>;
		FAABB Target;
		T Epsilon;

		TAABBEqualityMatcher(const FAABB& InTarget, const T InEpsilon = static_cast<T>(UE_KINDA_SMALL_NUMBER))
			: Target(InTarget)
			, Epsilon(InEpsilon)
		{
		}

		// Performs the test for this matcher
		bool match(const FAABB& ToCompare) const override
		{
			if (Epsilon == 0)
			{
				return ToCompare.Min() == Target.Min() && ToCompare.Max() == Target.Max();
			}
			return FVector::PointsAreNear(ToCompare.Min(), Target.Min(), Epsilon) && FVector::PointsAreNear(ToCompare.Max(), Target.Max(), Epsilon);
		}

		// Produces a string describing what this matcher does. It should
		// include any provided data (the begin/ end in this case) and
		// be written as if it were stating a fact (in the output it will be
		// preceded by the value under test).
		virtual std::string describe() const override
		{
			std::ostringstream Stream;
			Stream << "is equal to (" << Catch::StringMaker<FAABB>::convert(Target) << ") with epsilon " << Epsilon;
			return Stream.str();
		}
	};
	
	template <typename T, int D>
	TAABBEqualityMatcher<T, D> Equal(const Chaos::TAABB<T, D>& Target)
	{
		return TAABBEqualityMatcher<T, D>(Target, 0);
	}
	
	template <typename T, int D>
	TAABBEqualityMatcher<T, D> ApproxEq(const Chaos::TAABB<T, D>& Target, const T Epsilon = static_cast<T>(UE_KINDA_SMALL_NUMBER))
	{
		return TAABBEqualityMatcher<T, D>(Target, Epsilon);
	}
} // namespace Catch 