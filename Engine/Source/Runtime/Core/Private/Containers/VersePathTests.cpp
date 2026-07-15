// Copyright Epic Games, Inc. All Rights Reserved.
 
#if WITH_TESTS
 
#include "Containers/VersePath.h"
#include "Tests/TestHarnessAdapter.h"
 
TEST_CASE_NAMED(FVersePath_BasicEqualityHash, "System::Core::Containers::VersePath.BasicEqualityHash",
	"[Core][Containers][VersePath][SmokeFilter]")
{
	using UE::Core::FVersePath;

	FVersePath A, B;

	// Domain case-insensitive (normalized to lowercase), subpath case-sensitive
	CHECK(FVersePath::TryMake(A, TEXT("/User@Domain.com/Namespace/Project")));
	CHECK(FVersePath::TryMake(B, TEXT("/user@domain.com/Namespace/Project")));
	CHECK(A == B);
	CHECK(GetTypeHash(A) == GetTypeHash(B));

	CHECK(FVersePath::TryMake(A, TEXT("/user@domain.com/Namespace/Project")));
	CHECK(FVersePath::TryMake(B, TEXT("/user@domain.com/Namespace/project"))); // subpath differs by case
	CHECK(A != B);
	CHECK(GetTypeHash(A) != GetTypeHash(B));

	CHECK(UE::Core::FVersePath::TryMake(A, TEXT("/user@domain.com")));
	CHECK(UE::Core::FVersePath::TryMake(B, TEXT("/user@domain.com")));
	CHECK(A == B);
	CHECK(GetTypeHash(A) == GetTypeHash(B));

	CHECK(UE::Core::FVersePath::TryMake(A, TEXT("/user@domain.com")));
	CHECK(UE::Core::FVersePath::TryMake(B, TEXT("/User@Domain.com")));
	CHECK(A == B);
	CHECK(GetTypeHash(A) == GetTypeHash(B));

	CHECK(UE::Core::FVersePath::TryMake(A, TEXT("/user1@domain.com")));
	CHECK(UE::Core::FVersePath::TryMake(B, TEXT("/user2@verse.org")));
	CHECK(A != B);
	CHECK(GetTypeHash(A) != GetTypeHash(B));

	CHECK(UE::Core::FVersePath::TryMake(A, TEXT("/user1@domain.com/Project")));
	CHECK(UE::Core::FVersePath::TryMake(B, TEXT("/user2@verse.org/Project")));
	CHECK(A != B);
	CHECK(GetTypeHash(A) != GetTypeHash(B));
}

TEST_CASE_NAMED(FVersePath_IsBaseOf, "System::Core::Containers::VersePath.IsBaseOf",
	"[Core][Containers][VersePath][SmokeFilter]")
{
	using UE::Core::FVersePath;

	FVersePath Base, Equal, Child, Sibling, OtherDomain;
	REQUIRE(FVersePath::TryMake(Base, TEXT("/user@domain.com/path1")));
	REQUIRE(FVersePath::TryMake(Equal, TEXT("/user@domain.com/path1")));
	REQUIRE(FVersePath::TryMake(Child, TEXT("/user@domain.com/path1/path2/leaf")));
	REQUIRE(FVersePath::TryMake(Sibling, TEXT("/user@domain.com/path2/leaf")));
	REQUIRE(FVersePath::TryMake(OtherDomain, TEXT("/user@other.com/path1/leaf")));

	FStringView Leaf;
	CHECK(Base.IsBaseOf(Equal, &Leaf));
	CHECK(Leaf.IsEmpty());

	CHECK(Base.IsBaseOf(Child, &Leaf));
	CHECK(Leaf == FStringView(TEXT("path2/leaf")));

	CHECK_FALSE(Base.IsBaseOf(Sibling, &Leaf));
	CHECK(Leaf.IsEmpty());

	CHECK_FALSE(Base.IsBaseOf(OtherDomain, &Leaf));
	CHECK(Leaf.IsEmpty());
}

TEST_CASE_NAMED(FVersePath_Accessors, "System::Core::Containers::VersePath.Accessors",
	"[Core][Containers][VersePath][SmokeFilter]")
{
	using UE::Core::FVersePath;

	FVersePath P;
	REQUIRE(FVersePath::TryMake(P, TEXT("/User@Domain.com/Foo/Bar")));

	// Domain is normalized to lowercase
	CHECK(P.GetVerseDomain().Equals(FString(TEXT("user@domain.com")), ESearchCase::CaseSensitive));
	CHECK(P.GetVerseDomainAsStringView().Equals(TEXTVIEW("user@domain.com"), ESearchCase::CaseSensitive));
	CHECK(P.GetPathExceptVerseDomain().Equals(FString(TEXT("/Foo/Bar")), ESearchCase::CaseSensitive));
}

TEST_CASE_NAMED(FVersePath_Validation_FullPath, "System::Core::Containers::VersePath.Validation.FullPath",
	"[Core][Containers][VersePath][SmokeFilter]")
{
	using UE::Core::FVersePath;

	auto ExpectInvalid = [](const TCHAR* S, const TCHAR* Contains = nullptr)
	{
		FText Err;
		bool bOk = FVersePath::TryMake(*new UE::Core::FVersePath(), S, &Err);
		CHECK_FALSE(bOk);
		CHECK(!Err.IsEmpty());
		if (Contains)
		{
			CHECK(Err.BuildSourceString().Contains(Contains));
		}
	};

	auto ExpectValid = [](const TCHAR* S)
	{
		FText Err;
		UE::Core::FVersePath P;
		CHECK(FVersePath::TryMake(P, S, &Err));
		CHECK(Err.IsEmpty());
	};

	// Must start with '/'
	ExpectInvalid(TEXT("user@domain.com/Foo"), TEXT("must start with a slash"));

	// Empty domain
	ExpectInvalid(TEXT("/"), TEXT("Verse domain cannot be empty"));

	// Domain label: cannot start with '-' or '.'
	ExpectInvalid(TEXT("/-domain/Foo"), TEXT("cannot start with a dash"));
	ExpectInvalid(TEXT("/.domain/Foo"), TEXT("cannot start with a dot"));

	// Invalid chars in domain label; whitespace forbidden
	ExpectInvalid(TEXT("/dom ain/Foo"), TEXT("whitespace"));
	ExpectInvalid(TEXT("/do^main/Foo"), TEXT("cannot contain"));

	// '@' form: label@label is allowed; missing rhs label is invalid
	ExpectValid(TEXT("/domain.com"));
	ExpectValid(TEXT("/user@domain.com"));
	ExpectValid(TEXT("/user@domain.com/Foo"));
	ExpectInvalid(TEXT("/@domain.com"), TEXT(""));
	ExpectInvalid(TEXT("/user@"), TEXT("Invalid Verse domain"));

	// Subpath rules
	ExpectValid(TEXT("/domain/Foo"));
	ExpectValid(TEXT("/domain/_Foo0/Bar1")); // underscores and digits after first char OK
	ExpectInvalid(TEXT("/domain//Foo"), TEXT("cannot have consecutive slashes"));
	ExpectInvalid(TEXT("/domain/Foo/"), TEXT("cannot end with a slash"));

	// Ident rules: first char must be alpha or '_'; no hyphens or spaces
	ExpectInvalid(TEXT("/domain/1abc"), TEXT("cannot start with a number"));
	ExpectInvalid(TEXT("/domain/ab-c"), TEXT("cannot contain"));
	ExpectInvalid(TEXT("/domain/a b"), TEXT("cannot contain"));
}

TEST_CASE_NAMED(FVersePath_Validation_Helpers, "System::Core::Containers::VersePath.Validation.Helpers",
	"[Core][Containers][VersePath][SmokeFilter]")
{
	using UE::Core::FVersePath;

	// IsValidDomain (null-terminated)
	{
		FText Err;
		CHECK(FVersePath::IsValidDomain(TEXT("user@domain.com"), &Err));
		CHECK(Err.IsEmpty());

		CHECK_FALSE(FVersePath::IsValidDomain(TEXT("-bad"), &Err));
		CHECK(!Err.IsEmpty());
	}

	// IsValidDomain (bounded length)
	{
		const TCHAR* S = TEXT("user@domain.com@@@@"); // only first part should be considered by len
		FText Err;
		CHECK(FVersePath::IsValidDomain(S, 15, &Err)); // "user@domain.com" length
		CHECK(Err.IsEmpty());

		CHECK_FALSE(FVersePath::IsValidDomain(S, 0, &Err)); // empty
	}

	// IsValidSubpath
	{
		FText Err;
		CHECK(FVersePath::IsValidSubpath(TEXT("Foo/Bar"), &Err));
		CHECK(Err.IsEmpty());

		CHECK_FALSE(FVersePath::IsValidSubpath(TEXT(""), &Err)); // empty
		CHECK_FALSE(FVersePath::IsValidSubpath(TEXT("Foo/"), &Err)); // ends with slash
		CHECK_FALSE(FVersePath::IsValidSubpath(TEXT("Foo//Bar"), &Err)); // consecutive slashes
	}

	// IsValidIdent
	{
		FText Err;
		CHECK(FVersePath::IsValidIdent(TEXT("_Good1"), &Err));
		CHECK(Err.IsEmpty());

		CHECK_FALSE(FVersePath::IsValidIdent(TEXT("1Bad"), &Err));
		CHECK_FALSE(FVersePath::IsValidIdent(TEXT("Bad-Char"), &Err));
	}
}

TEST_CASE_NAMED(FVersePath_IdentifierUtils, "System::Core::Containers::VersePath.IdentifierUtils",
	"[Core][Containers][VersePath][SmokeFilter]")
{
	using namespace UE::Core;

	// Leading digit -> prefixed underscore; hyphens removed
	CHECK(MakeValidVerseIdentifier(TEXT("1234-5678")) == FString(TEXT("_12345678")));
	// whitespace -> single underscores (trim trailing)
	CHECK(MakeValidVerseIdentifier(TEXT("  foo   bar  ")) == FString(TEXT("foo_bar")));
	CHECK(MakeValidVerseIdentifier(TEXT("!!!")) == FString(TEXT("_")));
	// trailing underscore is trimmed
	CHECK(MakeValidVerseIdentifier(TEXT("abc ")) == FString(TEXT("abc")));
	CHECK(MakeValidVerseIdentifier(TEXT("550e8400-e29b-41d4-a716-446655440000")) ==
		FString(TEXT("_550e8400e29b41d4a716446655440000")));
}

#endif // WITH_TESTS
