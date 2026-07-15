// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_TESTS

#include "Sorting/NaturalSortKey.h"

#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "Tests/TestHarnessAdapter.h"

namespace UE::Editor::DataStorage::Testing
{
	namespace
	{
		// Byte-wise, length-aware compare of two label encodings.
		//
		// Does NOT go through FString::Compare / FStringView::Compare — both use strcmp-style
		// primitives that truncate at the first 0x00 byte, and BuildNaturalSortKey deliberately
		// emits embedded 0x00 bytes (hi-bytes of the length/zeros fields, plus the suffix
		// separator). CompareNaturalSortKeys is the only correct comparator for these keys.
		int32 KeyCompare(const TCHAR* A, const TCHAR* B)
		{
			return CompareNaturalSortKeys(
				BuildNaturalSortKey(FStringView(A)),
				BuildNaturalSortKey(FStringView(B)));
		}
	}
}

TEST_CASE_NAMED(
	TEDS_Outliner_NaturalSortKey_Tests,
	"Editor::DataStorage::Outliner::NaturalSortKey",
	"[ApplicationContextMask][EngineFilter]")
{
	using namespace UE::Editor::DataStorage;
	using namespace UE::Editor::DataStorage::Testing;

	// ==========================================================================
	// Part 1 — Algebraic properties
	// ==========================================================================
	// These establish that KeyCompare produces a strict weak ordering. Every
	// later test relies on these holding.

	SECTION("Reflexivity: every label compares equal to itself")
	{
		CHECK_MESSAGE(TEXT("'' == ''"),
			KeyCompare(TEXT(""), TEXT("")) == 0);
		CHECK_MESSAGE(TEXT("'Actor' == 'Actor'"),
			KeyCompare(TEXT("Actor"), TEXT("Actor")) == 0);
		CHECK_MESSAGE(TEXT("'actor' == 'actor'"),
			KeyCompare(TEXT("actor"), TEXT("actor")) == 0);
		CHECK_MESSAGE(TEXT("'AcToR_02' == 'AcToR_02' (mixed case + underscore + digits)"),
			KeyCompare(TEXT("AcToR_02"), TEXT("AcToR_02")) == 0);
		CHECK_MESSAGE(TEXT("'___' == '___'"),
			KeyCompare(TEXT("___"), TEXT("___")) == 0);
	}

	SECTION("Anti-symmetry: swapping args negates the sign")
	{
		auto IsAntiSymmetric = [](const TCHAR* A, const TCHAR* B) -> bool
		{
			const int32 Forward = KeyCompare(A, B);
			const int32 Reverse = KeyCompare(B, A);
			return (Forward < 0 && Reverse > 0)
				|| (Forward > 0 && Reverse < 0)
				|| (Forward == 0 && Reverse == 0);
		};
		CHECK_MESSAGE(TEXT("'Actor' / 'actor'"),      IsAntiSymmetric(TEXT("Actor"),     TEXT("actor")));
		CHECK_MESSAGE(TEXT("'Cube2' / 'Cube10'"),     IsAntiSymmetric(TEXT("Cube2"),     TEXT("Cube10")));
		CHECK_MESSAGE(TEXT("'Cube02' / 'Cube002'"),   IsAntiSymmetric(TEXT("Cube02"),    TEXT("Cube002")));
		CHECK_MESSAGE(TEXT("'A_B' / 'AB'"),           IsAntiSymmetric(TEXT("A_B"),       TEXT("AB")));
		CHECK_MESSAGE(TEXT("'' / 'A'"),               IsAntiSymmetric(TEXT(""),          TEXT("A")));
		CHECK_MESSAGE(TEXT("Equal pair stays equal both ways"),
			IsAntiSymmetric(TEXT("Actor"), TEXT("Actor")));
	}

	SECTION("Transitivity: chains close")
	{
		// Numeric chain: Cube1 < Cube2 < Cube10 < Cube100.
		CHECK_MESSAGE(TEXT("Cube1 < Cube2"),
			KeyCompare(TEXT("Cube1"), TEXT("Cube2")) < 0);
		CHECK_MESSAGE(TEXT("Cube2 < Cube10"),
			KeyCompare(TEXT("Cube2"), TEXT("Cube10")) < 0);
		CHECK_MESSAGE(TEXT("Cube10 < Cube100"),
			KeyCompare(TEXT("Cube10"), TEXT("Cube100")) < 0);
		CHECK_MESSAGE(TEXT("Transitive: Cube1 < Cube100"),
			KeyCompare(TEXT("Cube1"), TEXT("Cube100")) < 0);

		// Case chain: ACTOR < Actor < actor.
		CHECK_MESSAGE(TEXT("ACTOR < Actor"),
			KeyCompare(TEXT("ACTOR"), TEXT("Actor")) < 0);
		CHECK_MESSAGE(TEXT("Actor < actor"),
			KeyCompare(TEXT("Actor"), TEXT("actor")) < 0);
		CHECK_MESSAGE(TEXT("Transitive: ACTOR < actor"),
			KeyCompare(TEXT("ACTOR"), TEXT("actor")) < 0);

		// Mixed chain: '' < 'A' < 'AA' < 'AB'.
		CHECK_MESSAGE(TEXT("'' < 'A'"),
			KeyCompare(TEXT(""), TEXT("A")) < 0);
		CHECK_MESSAGE(TEXT("'A' < 'AA'"),
			KeyCompare(TEXT("A"), TEXT("AA")) < 0);
		CHECK_MESSAGE(TEXT("'AA' < 'AB'"),
			KeyCompare(TEXT("AA"), TEXT("AB")) < 0);
		CHECK_MESSAGE(TEXT("Transitive: '' < 'AB'"),
			KeyCompare(TEXT(""), TEXT("AB")) < 0);
	}

	// ==========================================================================
	// Part 2 — Natural digit-run semantics
	// ==========================================================================

	SECTION("Numeric magnitude: shorter number beats longer")
	{
		// The classic reason natural order exists in the first place.
		CHECK_MESSAGE(TEXT("Actor2 < Actor10"),
			KeyCompare(TEXT("Actor2"), TEXT("Actor10")) < 0);
		CHECK_MESSAGE(TEXT("Actor9 < Actor10"),
			KeyCompare(TEXT("Actor9"), TEXT("Actor10")) < 0);
		CHECK_MESSAGE(TEXT("Actor10 < Actor100"),
			KeyCompare(TEXT("Actor10"), TEXT("Actor100")) < 0);
		CHECK_MESSAGE(TEXT("Actor99 < Actor100"),
			KeyCompare(TEXT("Actor99"), TEXT("Actor100")) < 0);
		CHECK_MESSAGE(TEXT("Actor100 < Actor1000"),
			KeyCompare(TEXT("Actor100"), TEXT("Actor1000")) < 0);
	}

	SECTION("Leading zeros: numeric value still dominates")
	{
		CHECK_MESSAGE(TEXT("ACTOR02 < ACTOR3 (2 < 3 numerically)"),
			KeyCompare(TEXT("ACTOR02"), TEXT("ACTOR3")) < 0);
		CHECK_MESSAGE(TEXT("ACTOR1 < ACTOR02 (1 < 2)"),
			KeyCompare(TEXT("ACTOR1"), TEXT("ACTOR02")) < 0);
		CHECK_MESSAGE(TEXT("ACTOR09 < ACTOR10"),
			KeyCompare(TEXT("ACTOR09"), TEXT("ACTOR10")) < 0);
		CHECK_MESSAGE(TEXT("ACTOR1A < ACTOR02A (trailing letter tied, numeric 1 < 2)"),
			KeyCompare(TEXT("ACTOR1A"), TEXT("ACTOR02A")) < 0);
		CHECK_MESSAGE(TEXT("ACTOR0099 < ACTOR100 (99 < 100 regardless of zero padding)"),
			KeyCompare(TEXT("ACTOR0099"), TEXT("ACTOR100")) < 0);
	}

	SECTION("Numerically equal runs: fewer leading zeros sorts first")
	{
		// Same numeric value, different textual forms → shorter-original wins via
		// the leading-zero-count tiebreaker in the encoding.
		CHECK_MESSAGE(TEXT("Actor2 < Actor02"),
			KeyCompare(TEXT("Actor2"), TEXT("Actor02")) < 0);
		CHECK_MESSAGE(TEXT("Actor02 < Actor002"),
			KeyCompare(TEXT("Actor02"), TEXT("Actor002")) < 0);
		CHECK_MESSAGE(TEXT("Actor002 < Actor0002"),
			KeyCompare(TEXT("Actor002"), TEXT("Actor0002")) < 0);
		CHECK_MESSAGE(TEXT("Actor2A < Actor02A (suffix letter tied, run shorter wins)"),
			KeyCompare(TEXT("Actor2A"), TEXT("Actor02A")) < 0);
		CHECK_MESSAGE(TEXT("cube02 < cube002"),
			KeyCompare(TEXT("cube02"), TEXT("cube002")) < 0);
	}

	SECTION("All-zero runs ordered by length")
	{
		// "0" represents the number zero regardless of how many 0s precede it.
		// Ordering is then by original length (more zeros = later).
		CHECK_MESSAGE(TEXT("'0' < '00'"),
			KeyCompare(TEXT("0"), TEXT("00")) < 0);
		CHECK_MESSAGE(TEXT("'00' < '000'"),
			KeyCompare(TEXT("00"), TEXT("000")) < 0);
		CHECK_MESSAGE(TEXT("'000' < '0000'"),
			KeyCompare(TEXT("000"), TEXT("0000")) < 0);
		CHECK_MESSAGE(TEXT("Actor0 < Actor00"),
			KeyCompare(TEXT("Actor0"), TEXT("Actor00")) < 0);
		CHECK_MESSAGE(TEXT("Actor00 < Actor000"),
			KeyCompare(TEXT("Actor00"), TEXT("Actor000")) < 0);

		// Zero always sorts before any positive number with the same prefix.
		CHECK_MESSAGE(TEXT("Actor0 < Actor1"),
			KeyCompare(TEXT("Actor0"), TEXT("Actor1")) < 0);
		CHECK_MESSAGE(TEXT("Actor0000 < Actor1 (still 0 < 1)"),
			KeyCompare(TEXT("Actor0000"), TEXT("Actor1")) < 0);
	}

	SECTION("Multi-run labels compare run-by-run, left to right")
	{
		CHECK_MESSAGE(TEXT("Cube11_a1 < Cube011_a02 (first-run zero-count decides)"),
			KeyCompare(TEXT("Cube11_a1"), TEXT("Cube011_a02")) < 0);
		CHECK_MESSAGE(TEXT("Cube11_a1 < Cube11_a2 (first runs tied, second 1 < 2)"),
			KeyCompare(TEXT("Cube11_a1"), TEXT("Cube11_a2")) < 0);
		CHECK_MESSAGE(TEXT("Cube11_a2 < Cube011_a02 (first-run zero-count decides)"),
			KeyCompare(TEXT("Cube11_a2"), TEXT("Cube011_a02")) < 0);
		CHECK_MESSAGE(TEXT("Cube11_a < Cube011_a (trailing letter tied, zero-count decides)"),
			KeyCompare(TEXT("Cube11_a"), TEXT("Cube011_a")) < 0);

		// A differing EARLIER run wins over LATER runs regardless of size.
		CHECK_MESSAGE(TEXT("A1B10 < A2B1 (first run 1 < 2 wins over later B10 vs B1)"),
			KeyCompare(TEXT("A1B10"), TEXT("A2B1")) < 0);
		CHECK_MESSAGE(TEXT("A1B1 < A1B2 (first runs tied, second decides)"),
			KeyCompare(TEXT("A1B1"), TEXT("A1B2")) < 0);
	}

	SECTION("Digit runs at different positions (start, middle, end)")
	{
		// Leading digit run.
		CHECK_MESSAGE(TEXT("'2Actor' < '10Actor'"),
			KeyCompare(TEXT("2Actor"), TEXT("10Actor")) < 0);
		// Middle digit run.
		CHECK_MESSAGE(TEXT("'Ac2tor' < 'Ac10tor'"),
			KeyCompare(TEXT("Ac2tor"), TEXT("Ac10tor")) < 0);
		// Trailing digit run.
		CHECK_MESSAGE(TEXT("'Actor2' < 'Actor10'"),
			KeyCompare(TEXT("Actor2"), TEXT("Actor10")) < 0);
		// Digit run followed by digit run (via underscore separator).
		CHECK_MESSAGE(TEXT("'1_2' < '1_10' (second run 2 < 10)"),
			KeyCompare(TEXT("1_2"), TEXT("1_10")) < 0);
	}

	// ==========================================================================
	// Part 3 — Case tiebreaker (deterministic, suffix-based)
	// ==========================================================================
	// Claim: for any two labels A, B with ToLower(A) == ToLower(B), the sign of
	// KeyCompare(A, B) equals sign(A[k] - B[k]) where k is the first index at
	// which the raw labels differ. For ASCII letters this reduces to "uppercase
	// wins at the first differing position" since 'A'..'Z' (0x41..0x5A) precede
	// 'a'..'z' (0x61..0x7A) in byte order.

	SECTION("Case-only differences: uppercase wins at the first differing byte")
	{
		// Position coverage — the rule must fire at every position, not just pos 0.
		CHECK_MESSAGE(TEXT("'Actor' < 'actor' (pos 0)"),
			KeyCompare(TEXT("Actor"), TEXT("actor")) < 0);
		CHECK_MESSAGE(TEXT("'ACTOR' < 'Actor' (pos 1)"),
			KeyCompare(TEXT("ACTOR"), TEXT("Actor")) < 0);
		CHECK_MESSAGE(TEXT("'acTor' < 'actor' (pos 2)"),
			KeyCompare(TEXT("acTor"), TEXT("actor")) < 0);
		CHECK_MESSAGE(TEXT("'actoR' < 'actor' (pos 4, last char)"),
			KeyCompare(TEXT("actoR"), TEXT("actor")) < 0);

		// Original reported pairs resolve deterministically to uppercase-first.
		CHECK_MESSAGE(TEXT("'Bactor' < 'bActor'"),
			KeyCompare(TEXT("Bactor"), TEXT("bActor")) < 0);
	}

	SECTION("All 8 case permutations of 'abc' sort by raw suffix bytes")
	{
		// Expected: ABC < ABc < AbC < Abc < aBC < aBc < abC < abc
		// The natural portion ties for all 8; the suffix is just the raw label,
		// so the sort order is exactly raw lexicographic byte order.
		const TCHAR* const Ordered[] =
		{
			TEXT("ABC"), TEXT("ABc"), TEXT("AbC"), TEXT("Abc"),
			TEXT("aBC"), TEXT("aBc"), TEXT("abC"), TEXT("abc"),
		};
		const int32 Count = UE_ARRAY_COUNT(Ordered);
		for (int32 I = 0; I + 1 < Count; ++I)
		{
			CHECK_MESSAGE(
				*FString::Printf(TEXT("'%s' < '%s'"), Ordered[I], Ordered[I + 1]),
				KeyCompare(Ordered[I], Ordered[I + 1]) < 0);
		}
		CHECK_MESSAGE(TEXT("Endpoints: 'ABC' < 'abc'"),
			KeyCompare(TEXT("ABC"), TEXT("abc")) < 0);
	}

	SECTION("Case rule uses the FIRST differing byte, not a count")
	{
		// 'XyzABC' vs 'XyzAbc' — first diff at pos 4, B < b → XyzABC wins.
		// The later three lowers in 'XyzAbc' don't matter.
		CHECK_MESSAGE(TEXT("'XyzABC' < 'XyzAbc' (one early upper beats three later lowers)"),
			KeyCompare(TEXT("XyzABC"), TEXT("XyzAbc")) < 0);
		// Equal upper/lower count, pos 0 decides.
		CHECK_MESSAGE(TEXT("'AaAa' < 'aAaA' (equal case counts, pos 0 wins)"),
			KeyCompare(TEXT("AaAa"), TEXT("aAaA")) < 0);
		// Three upper vs one upper at pos 0, upper wins immediately regardless.
		CHECK_MESSAGE(TEXT("'Aaaa' < 'aAAA' (pos 0 decides)"),
			KeyCompare(TEXT("Aaaa"), TEXT("aAAA")) < 0);
	}

	SECTION("Case tiebreaker composes with digits and underscores")
	{
		// With digits — numerics tie, suffix case decides.
		CHECK_MESSAGE(TEXT("'Actor02' < 'actor02'"),
			KeyCompare(TEXT("Actor02"), TEXT("actor02")) < 0);
		CHECK_MESSAGE(TEXT("'ACTOR02' < 'Actor02'"),
			KeyCompare(TEXT("ACTOR02"), TEXT("Actor02")) < 0);
		CHECK_MESSAGE(TEXT("'Actor_02' < 'actor_02'"),
			KeyCompare(TEXT("Actor_02"), TEXT("actor_02")) < 0);

		// With underscores — natural ties (underscore skipped), suffix case decides.
		CHECK_MESSAGE(TEXT("'A_B' < 'a_B'"),
			KeyCompare(TEXT("A_B"), TEXT("a_B")) < 0);
		CHECK_MESSAGE(TEXT("'A_B' < 'A_b'"),
			KeyCompare(TEXT("A_B"), TEXT("A_b")) < 0);
		CHECK_MESSAGE(TEXT("'A_B' < 'a_b' (both case positions flip)"),
			KeyCompare(TEXT("A_B"), TEXT("a_b")) < 0);
	}

	SECTION("Natural order dominates case tiebreaker")
	{
		// When the natural portions differ, case never enters the picture —
		// even when the "case-losing" side is ALL uppercase.
		CHECK_MESSAGE(TEXT("'actor2' < 'ACTOR10' (2 < 10 wins; case irrelevant)"),
			KeyCompare(TEXT("actor2"), TEXT("ACTOR10")) < 0);
		CHECK_MESSAGE(TEXT("'actor03' > 'ACTOR2' (3 > 2 wins; case irrelevant)"),
			KeyCompare(TEXT("actor03"), TEXT("ACTOR2")) > 0);
		CHECK_MESSAGE(TEXT("'Actor02' < 'actor3' (2 < 3 wins)"),
			KeyCompare(TEXT("Actor02"), TEXT("actor3")) < 0);
	}

	// ==========================================================================
	// Part 4 — Underscore handling
	// ==========================================================================

	SECTION("Underscores: skipped in natural portion, preserved in suffix")
	{
		// Natural tie on case-fold, suffix bytes decide. '_' (0x5F) sorts below
		// 't' (0x74), so 'Ac_tor' comes before 'Actor'.
		CHECK_MESSAGE(TEXT("'Ac_tor' < 'Actor' (natural ties, suffix '_' < 't')"),
			KeyCompare(TEXT("Ac_tor"), TEXT("Actor")) < 0);
		// Same principle but with the underscore earlier — also beats 'Actor'.
		CHECK_MESSAGE(TEXT("'A_ctor' < 'Actor' (suffix '_' at pos 1 beats 'c')"),
			KeyCompare(TEXT("A_ctor"), TEXT("Actor")) < 0);
		// Leading underscores push the suffix 'A' further back, but the natural
		// portion is still "actor" for both — suffix pos 0 is '_' (0x5F) vs 'A' (0x41)
		// and 'A' < '_' → 'Actor' wins.
		CHECK_MESSAGE(TEXT("'Actor' < '_Actor' (suffix 'A' (0x41) < '_' (0x5F))"),
			KeyCompare(TEXT("Actor"), TEXT("_Actor")) < 0);
	}

	SECTION("Underscore-separated digit runs are parsed independently")
	{
		// 'A_1_2' → two single-digit runs.
		// 'A_12'  → one two-digit run.
		// This matches UE::ComparisonUtility::CompareNaturalOrder's handling.
		CHECK_MESSAGE(TEXT("'A_1_2' < 'A_12' (two runs of length 1 sort below one of length 2)"),
			KeyCompare(TEXT("A_1_2"), TEXT("A_12")) < 0);
		CHECK_MESSAGE(TEXT("'A_1_B' < 'A_10' (run-then-letter vs run-of-10)"),
			KeyCompare(TEXT("A_1_B"), TEXT("A_10")) < 0);
	}

	// ==========================================================================
	// Part 5 — Empty, prefix, and boundary cases
	// ==========================================================================

	SECTION("Empty and underscore-only labels")
	{
		// Natural portion is empty for all of these; the suffix (preserved raw
		// label) decides the order purely by length / content.
		CHECK_MESSAGE(TEXT("'' < '_'"),
			KeyCompare(TEXT(""), TEXT("_")) < 0);
		CHECK_MESSAGE(TEXT("'_' < '__'"),
			KeyCompare(TEXT("_"), TEXT("__")) < 0);
		CHECK_MESSAGE(TEXT("'__' < '___'"),
			KeyCompare(TEXT("__"), TEXT("___")) < 0);

		// An empty label still produces a non-empty key (the 0x00 separator +
		// empty suffix = 1 TCHAR). This is load-bearing for the natural-vs-suffix
		// byte compare at the boundary.
		CHECK_MESSAGE(TEXT("Empty label key length == 1 (just the 0x00 separator)"),
			BuildNaturalSortKey(FStringView(TEXT(""))).Len() == 1);
	}

	SECTION("Prefix relations: shorter wins when it's a strict prefix")
	{
		CHECK_MESSAGE(TEXT("'' < 'A'"),
			KeyCompare(TEXT(""), TEXT("A")) < 0);
		CHECK_MESSAGE(TEXT("'A' < 'AB'"),
			KeyCompare(TEXT("A"), TEXT("AB")) < 0);
		CHECK_MESSAGE(TEXT("'Actor' < 'Actor2' (shorter natural portion wins)"),
			KeyCompare(TEXT("Actor"), TEXT("Actor2")) < 0);
		CHECK_MESSAGE(TEXT("'Actor2' < 'Actor2A' (natural continues with a letter)"),
			KeyCompare(TEXT("Actor2"), TEXT("Actor2A")) < 0);
	}

	SECTION("Pure-digit labels vs letter-prefixed labels")
	{
		// The digit-run sentinel (0x01) sits below any printable character,
		// so labels that start with a digit sort before labels that start with
		// a letter or symbol.
		CHECK_MESSAGE(TEXT("'2' < 'A'"),
			KeyCompare(TEXT("2"), TEXT("A")) < 0);
		CHECK_MESSAGE(TEXT("'10' < 'A'"),
			KeyCompare(TEXT("10"), TEXT("A")) < 0);
		CHECK_MESSAGE(TEXT("'100' < 'a'"),
			KeyCompare(TEXT("100"), TEXT("a")) < 0);
		// Pure-digit labels sort numerically among themselves.
		CHECK_MESSAGE(TEXT("'2' < '10'"),
			KeyCompare(TEXT("2"), TEXT("10")) < 0);
		CHECK_MESSAGE(TEXT("'9' < '10'"),
			KeyCompare(TEXT("9"), TEXT("10")) < 0);
	}

	// ==========================================================================
	// Part 6 — Encoded key format lock-in (byte-level)
	// ==========================================================================

	SECTION("Encoded key format for 'ACTOR02'")
	{
		// Layout:
		//   [lowercased letters] [digit-run: 0x01 hi-sig-len lo-sig-len digits hi-zeros lo-zeros]
		//   [0x00 separator]     [raw case-preserving label]
		// For "ACTOR02":
		//   5 lowercased letters + 6 digit-run bytes + 1 separator + 7 raw chars = 19 TCHARs.
		const FString Key = BuildNaturalSortKey(FStringView(TEXT("ACTOR02")));
		CHECK_MESSAGE(TEXT("Key length == 19"), Key.Len() == 19);
		if (Key.Len() == 19)
		{
			// Natural portion (case-folded letters + encoded digit run).
			CHECK_MESSAGE(TEXT("[0]  'a'"),                          Key[0]  == TEXT('a'));
			CHECK_MESSAGE(TEXT("[1]  'c'"),                          Key[1]  == TEXT('c'));
			CHECK_MESSAGE(TEXT("[2]  't'"),                          Key[2]  == TEXT('t'));
			CHECK_MESSAGE(TEXT("[3]  'o'"),                          Key[3]  == TEXT('o'));
			CHECK_MESSAGE(TEXT("[4]  'r'"),                          Key[4]  == TEXT('r'));
			CHECK_MESSAGE(TEXT("[5]  0x01 digit-run sentinel"),      Key[5]  == TCHAR(0x01));
			CHECK_MESSAGE(TEXT("[6]  0x00 sig-len hi"),              Key[6]  == TCHAR(0x00));
			CHECK_MESSAGE(TEXT("[7]  0x01 sig-len lo = 1 significant digit"), Key[7]  == TCHAR(0x01));
			CHECK_MESSAGE(TEXT("[8]  '2' (leading '0' stripped)"),   Key[8]  == TEXT('2'));
			CHECK_MESSAGE(TEXT("[9]  0x00 zeros hi"),                Key[9]  == TCHAR(0x00));
			CHECK_MESSAGE(TEXT("[10] 0x01 zeros lo = 1 stripped zero"), Key[10] == TCHAR(0x01));
			// Suffix (0x00 separator + raw label verbatim).
			CHECK_MESSAGE(TEXT("[11] 0x00 suffix separator"),        Key[11] == TCHAR(0x00));
			CHECK_MESSAGE(TEXT("[12] 'A' (raw, preserved)"),         Key[12] == TEXT('A'));
			CHECK_MESSAGE(TEXT("[13] 'C'"),                          Key[13] == TEXT('C'));
			CHECK_MESSAGE(TEXT("[14] 'T'"),                          Key[14] == TEXT('T'));
			CHECK_MESSAGE(TEXT("[15] 'O'"),                          Key[15] == TEXT('O'));
			CHECK_MESSAGE(TEXT("[16] 'R'"),                          Key[16] == TEXT('R'));
			CHECK_MESSAGE(TEXT("[17] '0' (raw, leading zero preserved in suffix)"), Key[17] == TEXT('0'));
			CHECK_MESSAGE(TEXT("[18] '2'"),                          Key[18] == TEXT('2'));
		}
	}

	// ==========================================================================
	// Part 7 — End-to-end ordering over a mixed list
	// ==========================================================================

	SECTION("End-to-end: mixed labels sort into the expected sequence")
	{
		// A curated list in the expected ascending natural order. The loop
		// checks every consecutive pair; transitivity covers the rest.
		const TCHAR* const Ordered[] =
		{
			// Empties and underscores (natural portion empty; suffix length wins).
			TEXT(""),
			TEXT("_"),
			TEXT("__"),

			// Pure digits (digit sentinel 0x01 sorts below letters).
			TEXT("1"),
			TEXT("2"),
			TEXT("9"),
			TEXT("10"),
			TEXT("100"),

			// Case tiebreaker on case-fold-identical letter-only labels.
			TEXT("Actor"),
			TEXT("actor"),

			// Actor + digit-run variants: zero, leading zeros, non-zero ascending.
			TEXT("Actor0"),
			TEXT("Actor00"),
			TEXT("Actor1"),
			TEXT("Actor2"),
			TEXT("Actor02"),
			TEXT("Actor002"),
			TEXT("ACTOR_3"),
			TEXT("Actor9"),
			TEXT("Actor10"),
			TEXT("Actor100"),

			// Letter-first labels in alphabetical order.
			TEXT("Bone"),
			TEXT("Weapon"),
		};
		const int32 Count = UE_ARRAY_COUNT(Ordered);
		for (int32 I = 0; I + 1 < Count; ++I)
		{
			CHECK_MESSAGE(
				*FString::Printf(TEXT("Expected '%s' < '%s'"), Ordered[I], Ordered[I + 1]),
				KeyCompare(Ordered[I], Ordered[I + 1]) < 0);
		}
	}
}

#endif // WITH_TESTS
