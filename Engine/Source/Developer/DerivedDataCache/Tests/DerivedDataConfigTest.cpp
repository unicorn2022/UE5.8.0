// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_LOW_LEVEL_TESTS

#include "DerivedDataConfig.h"

#include "Containers/Array.h"
#include "TestHarness.h"
#include "Tests/WarnFilterScope.h"

namespace UE::DerivedData
{

TEST_CASE("DerivedData::Config", "[DerivedData]")
{
	SECTION("ParseQuotedString")
	{
		TStringBuilder<64> Scratch;
		CHECK(Config::ParseQuotedString(TEXTVIEW(""), Scratch) == TEXTVIEW(""));
		CHECK(Config::ParseQuotedString(TEXTVIEW(R"(A)"), Scratch) == TEXTVIEW(R"(A)"));
		CHECK(Config::ParseQuotedString(TEXTVIEW(R"(A B C)"), Scratch) == TEXTVIEW(R"(A B C)"));
		CHECK(Config::ParseQuotedString(TEXTVIEW(R"(A " C)"), Scratch) == TEXTVIEW(R"(A " C)"));
		CHECK(Config::ParseQuotedString(TEXTVIEW(R"(\ A " C \)"), Scratch) == TEXTVIEW(R"(\ A " C \)"));
		CHECK(Config::ParseQuotedString(TEXTVIEW(R"('A B C')"), Scratch) == TEXTVIEW(R"('A B C')"));
		CHECK(Config::ParseQuotedString(TEXTVIEW(R"("A B C")"), Scratch) == TEXTVIEW(R"(A B C)"));
		CHECK(Config::ParseQuotedString(TEXTVIEW(R"("A B C)"), Scratch) == TEXTVIEW(R"(A B C)"));
		CHECK(Config::ParseQuotedString(TEXTVIEW(R"(A B C")"), Scratch) == TEXTVIEW(R"(A B C)"));
		CHECK(Scratch.Len() == 0);
		CHECK(Config::ParseQuotedString(TEXTVIEW(R"("A \"B\" C")"), Scratch) == TEXTVIEW(R"(A "B" C)"));
		CHECK(Config::ParseQuotedString(TEXTVIEW(R"("A \"\" C")"), Scratch) == TEXTVIEW(R"(A "" C)"));
		CHECK(Config::ParseQuotedString(TEXTVIEW(R"("\\")"), Scratch) == TEXTVIEW(R"(\)"));
		CHECK(Config::ParseQuotedString(TEXTVIEW(R"("\")"), Scratch) == TEXTVIEW(R"()"));
		CHECK(Config::ParseQuotedString(TEXTVIEW(R"("\'\"\?\\\a\b\f\n\r\t\v")"), Scratch) == TEXTVIEW("\'\"\?\\\a\b\f\n\r\t\v"));
	}

	SECTION("TrySplitNameAndConfig")
	{
		const auto TestTrySplitNameAndConfig = [](const TCHAR* Input, const TCHAR* ExpectedName, const TCHAR* ExpectedConfig)
		{
			CAPTURE(Input);
			FStringView Name, Config;
			bool bOk = Config::TrySplitNameAndConfig(Input, Name, Config);
			CHECK(bOk == (ExpectedName && ExpectedConfig));
			if (bOk)
			{
				CHECK(Name == ExpectedName);
				CHECK(Config == ExpectedConfig);
			}
		};
		TestTrySplitNameAndConfig(TEXT(""), nullptr, nullptr);
		TestTrySplitNameAndConfig(TEXT("-"), nullptr, nullptr);
		TestTrySplitNameAndConfig(TEXT("A-B"), nullptr, nullptr);
		TestTrySplitNameAndConfig(TEXT("A-B(C)"), nullptr, nullptr);
		TestTrySplitNameAndConfig(TEXT("A.B"), nullptr, nullptr);
		TestTrySplitNameAndConfig(TEXT("A.B(C)"), nullptr, nullptr);
		TestTrySplitNameAndConfig(TEXT("ABC"), TEXT("ABC"), TEXT(""));
		TestTrySplitNameAndConfig(TEXT("ABC()"), TEXT("ABC"), TEXT("()"));
		TestTrySplitNameAndConfig(TEXT(" ABC "), TEXT("ABC"), TEXT(""));
		TestTrySplitNameAndConfig(TEXT(" ABC () "), TEXT("ABC"), TEXT("()"));
		TestTrySplitNameAndConfig(TEXT("()"), TEXT(""), TEXT("()"));
		TestTrySplitNameAndConfig(TEXT(" () "), TEXT(""), TEXT("()"));
		TestTrySplitNameAndConfig(TEXT("(A=1, B=2)"), TEXT(""), TEXT("(A=1, B=2)"));
	}
}

TEST_CASE("DerivedData::CacheConfig", "[DerivedData]")
{
	SECTION("TryFindCacheGraphConfig")
	{
		const auto TestFindGraph = [](const TCHAR* GraphNameOrConfig, const TCHAR* ExpectedConfig) -> bool
		{
			CAPTURE(GraphNameOrConfig);
			TStringBuilder<1024> Config;
			bool bFound = TryFindCacheGraphConfig(GraphNameOrConfig, Config);
			CHECK(Config.ToView() == MakeStringView(ExpectedConfig));
			return bFound;
		};

		CHECK_FALSE(TestFindGraph(TEXT("TestMissingCacheGraph"), TEXT("")));
		for (int32 Index = 1; Index <= 6; ++Index)
		{
			CHECK_FALSE(TestFindGraph(*WriteToString<32>(TEXTVIEW("TestInvalidCacheGraph"), Index), TEXT("")));
		}
		CHECK(TestFindGraph(TEXT("TestCacheGraph0"), TEXT("()")));
		CHECK(TestFindGraph(TEXT("TestCacheGraph1"), TEXT("(TestStore1)")));
		CHECK(TestFindGraph(TEXT("TestCacheGraph2"), TEXT("(TestStore1, TestStore2, TestStore3)")));
		CHECK(TestFindGraph(TEXT("TestCacheGraph3"), TEXT("(TestStore1, TestStore2, TestStore3)")));
		CHECK(TestFindGraph(TEXT("TestCacheGraph4"), TEXT("(TestStore1, TestStore2, TestStore3)")));
		CHECK(TestFindGraph(TEXT("TestCacheGraph5"), TEXT("(TestStore1, TestStore2, TestStore3)")));
		CHECK(TestFindGraph(TEXT("TestCacheGraph6"), TEXT("(TestStore1)")));
		CHECK(TestFindGraph(TEXT("TestCacheGraph7"), TEXT("(TestStore1)")));
		CHECK(TestFindGraph(TEXT("TestCacheGraph8"), TEXT("(TestStore1)")));

		{
			CHECK_LOG_SCOPE(LogDerivedDataCache, Warning, "The cache graph 'TestCacheGraph9' is deprecated. Use TestCacheGraph1.");
			CHECK(TestFindGraph(TEXT("TestCacheGraph9"), TEXT("(TestStore1)")));
		}

		{
			CHECK_LOG_SCOPE(LogDerivedDataCache, Warning, "Configuring cache graph 'TestLegacyCacheGraph1' in config section [TestLegacyCacheGraph1] of the 'Engine' config is deprecated. Migrate the configuration to the [DerivedDataCacheGraphs] section of the 'Engine' config.");
			CHECK_LOG_SCOPE(LogDerivedDataCache, Warning, "The cache graph 'TestLegacyCacheGraph1' contains cache store 'Root' of deprecated type 'KeyLength'.");
			CHECK_LOG_SCOPE(LogDerivedDataCache, Warning, "The cache graph 'TestLegacyCacheGraph1' contains cache store 'Verify' of deprecated type 'Verify'.");
			CHECK_LOG_SCOPE(LogDerivedDataCache, Warning, "The cache graph 'TestLegacyCacheGraph1' contains cache store 'AsyncPut' of deprecated type 'AsyncPut'.");
			CHECK(TestFindGraph(TEXT("TestLegacyCacheGraph1"), TEXT("(TestLegacyStore1, TestStore2, TestStore8)")));
		}

		CHECK(TestFindGraph(TEXT("(TestStore1)"), TEXT("(TestStore1)")));
		CHECK(TestFindGraph(TEXT(" ( TestStore1 , TestStore2 ) "), TEXT("(TestStore1, TestStore2)")));
		CHECK(TestFindGraph(TEXT(" ( TestStore1 ( Key1 = CustomValue ) , TestStore2(CustomValue1, Key2=CustomValue2) ) "), TEXT("(TestStore1, TestStore2)")));

		CHECK_FALSE(TestFindGraph(TEXT("TestCacheGraph1(TestStore1)"), TEXT("")));
		CHECK_FALSE(TestFindGraph(TEXT("TestCacheGraph1(TestStore1,TestStore2)"), TEXT("")));
	}

	SECTION("TryFindCacheStoreConfig")
	{
		const auto TestFindStore = [](const TCHAR* StoreName, const TCHAR* GraphNameOrConfig, const TCHAR* ExpectedConfig) -> bool
		{
			CAPTURE(StoreName, GraphNameOrConfig);
			TStringBuilder<1024> Config;
			bool bFound = TryFindCacheStoreConfig(StoreName, GraphNameOrConfig, Config);
			CHECK(Config.ToView() == MakeStringView(ExpectedConfig));
			return bFound;
		};

		CHECK_FALSE(TestFindStore(TEXT("TestMissing"), nullptr, TEXT("")));
		CHECK_FALSE(TestFindStore(TEXT("TestMissing"), TEXT("TestCacheGraph1"), TEXT("")));

		CHECK_FALSE(TestFindStore(TEXT("TestInvalidStore1"), nullptr, TEXT("")));
		CHECK_FALSE(TestFindStore(TEXT("TestInvalidStore1"), TEXT("TestInvalidCacheStore1"), TEXT("")));
		CHECK_FALSE(TestFindStore(TEXT("TestInvalidStore1"), TEXT("TestInvalidCacheStore2"), TEXT("")));
		CHECK_FALSE(TestFindStore(TEXT("TestInvalidStore1"), TEXT("TestInvalidCacheStore3"), TEXT("")));
		CHECK_FALSE(TestFindStore(TEXT("TestInvalidStore2"), TEXT("TestInvalidCacheStore3"), TEXT("")));
		CHECK_FALSE(TestFindStore(TEXT("TestInvalidStore3"), nullptr, TEXT("")));

		CHECK_FALSE(TestFindStore(TEXT("TestInlineStore"), nullptr, TEXT("")));
		CHECK(TestFindStore(TEXT("TestInlineStore"), TEXT("TestCacheGraphInlineStore1"), TEXT("(Key1=Value1(A=1, B=2, C), Key2=Value2, Key3=D(123))")));
		CHECK(TestFindStore(TEXT("TestInlineStore"), TEXT("TestCacheGraphInlineStore2"), TEXT("(Key1=Value1(A=1, B=2, C), Key2=Value2, Key3=D(123))")));
		CHECK_FALSE(TestFindStore(TEXT("TestMissing"), TEXT("TestCacheGraphInlineStore2"), TEXT("")));

		CHECK(TestFindStore(TEXT("TestStore1"), nullptr, TEXT("(Key1=Store1Value, Key2=Store1Value, ValueOnly, (X=\"A=1, \\\"B\\\"=2, C=3\"))")));
		CHECK(TestFindStore(TEXT("TestStore1"), TEXT("TestCacheGraph3"), TEXT("(Key1=Store1Value, Key2=Store1Value, ValueOnly, (X=\"A=1, \\\"B\\\"=2, C=3\"))")));

		CHECK(TestFindStore(TEXT("TestStore2"), nullptr, TEXT("(Key1=Store2Value, Key1=Store1Value, Key2=Store1Value, ValueOnly, (X=\"A=1, \\\"B\\\"=2, C=3\"))")));
		CHECK(TestFindStore(TEXT("TestStore2"), TEXT("TestCacheGraph3"), TEXT("(Key1=Store2Value, Key1=Store1Value, Key2=Store1Value, ValueOnly, (X=\"A=1, \\\"B\\\"=2, C=3\"))")));

		CHECK(TestFindStore(TEXT("TestStore3"), nullptr, TEXT("(ValueOnly, Key1=Store3Value, Key2=Store3Value, Key3=(A=1, B=2, C))")));
		CHECK(TestFindStore(TEXT("TestStore3"), TEXT("TestCacheGraph5"), TEXT("(Key1=Value1, Key2=Value2, ValueOnly, Key1=Store3Value, Key2=Store3Value, Key3=(A=1, B=2, C))")));

		CHECK(TestFindStore(TEXT("TestStore4"), nullptr, TEXT("(Key1=Store4Value, Key1=Store2Value, Key1=Store1Value, Key2=Store1Value, ValueOnly, (X=\"A=1, \\\"B\\\"=2, C=3\"))")));
		CHECK(TestFindStore(TEXT("TestStore4"), TEXT("TestCacheGraph5"), TEXT("(Key1=Store4Value, Key1=Store2Value, Key1=Store1Value, Key2=Store1Value, ValueOnly, (X=\"A=1, \\\"B\\\"=2, C=3\"))")));

		CHECK(TestFindStore(TEXT("TestStore1"), TEXT("TestCacheGraph6"), TEXT("(Key1=Value1)")));
		CHECK(TestFindStore(TEXT("TestStore1"), TEXT("TestCacheGraph7"), TEXT("(Key1=Value1, Key1=Store5Value, Key2=Store5Value)")));
		CHECK(TestFindStore(TEXT("TestStore1"), TEXT("TestCacheGraph8"), TEXT("(Key1=Store1Value, Key2=Store1Value, ValueOnly, (X=\"A=1, \\\"B\\\"=2, C=3\"))")));

		{
			CHECK_LOG_SCOPE(LogDerivedDataCache, Warning, "The cache store 'TestStore7' is deprecated. Use TestStore1.");
			CHECK(TestFindStore(TEXT("TestStore7"), nullptr, TEXT("(Key1=Store1Value, Key2=Store1Value, ValueOnly, (X=\"A=1, \\\"B\\\"=2, C=3\"))")));
		}

		{
			CHECK_LOG_SCOPE(LogDerivedDataCache, Warning, "Configuring cache store 'TestLegacyStore1' in config section [TestLegacyCacheGraph1] of the 'Engine' config is deprecated. Migrate the configuration to the [DerivedDataCacheStores] section of the 'Engine' config.");
			CHECK(TestFindStore(TEXT("TestLegacyStore1"), TEXT("TestLegacyCacheGraph1"), TEXT("(Type=Zen, Key1=LegacyStore1Value, Key2=LegacyStore1Value)")));
		}
		{
			CHECK_LOG_SCOPE(LogDerivedDataCache, Warning, "Configuring cache store 'TestStore2' in config section [TestLegacyCacheGraph1] of the 'Engine' config is deprecated. Migrate the configuration to the [DerivedDataCacheStores] section of the 'Engine' config.");
			CHECK(TestFindStore(TEXT("TestStore2"), TEXT("TestLegacyCacheGraph1"), TEXT("(Type=Zen, Key1=LegacyStore2Value)")));
		}
		CHECK(TestFindStore(TEXT("TestStore8"), TEXT("TestLegacyCacheGraph1"), TEXT("(Type=Cloud)")));

		CHECK(TestFindStore(TEXT("TestStore1"), TEXT("(TestStore1)"),
			TEXT("(Key1=Store1Value, Key2=Store1Value, ValueOnly, (X=\"A=1, \\\"B\\\"=2, C=3\"))")));
		CHECK(TestFindStore(TEXT("TestStore1"), TEXT(" ( TestStore1 , TestStore2 ) "),
			TEXT("(Key1=Store1Value, Key2=Store1Value, ValueOnly, (X=\"A=1, \\\"B\\\"=2, C=3\"))")));
		CHECK(TestFindStore(TEXT("TestStore2"), TEXT(" ( TestStore1 , TestStore2 ) "),
			TEXT("(Key1=Store2Value, Key1=Store1Value, Key2=Store1Value, ValueOnly, (X=\"A=1, \\\"B\\\"=2, C=3\"))")));
		CHECK(TestFindStore(TEXT("TestStore1"), TEXT(" ( TestStore1 ( Key1 = CustomValue ) , TestStore2(CustomValue1, Key2=CustomValue2) ) "),
			TEXT("(Key1=CustomValue, Key1=Store1Value, Key2=Store1Value, ValueOnly, (X=\"A=1, \\\"B\\\"=2, C=3\"))")));
		CHECK(TestFindStore(TEXT("TestStore2"), TEXT(" ( TestStore1 ( Key1 = CustomValue ) , TestStore2(CustomValue1, Key2=CustomValue2) ) "),
			TEXT("(CustomValue1, Key2=CustomValue2, Key1=Store2Value, Key1=Store1Value, Key2=Store1Value, ValueOnly, (X=\"A=1, \\\"B\\\"=2, C=3\"))")));

		CHECK_FALSE(TestFindStore(TEXT("TestStore1"), TEXT("TestCacheGraph1(TestStore1)"), TEXT("")));
		CHECK_FALSE(TestFindStore(TEXT("TestStore1"), TEXT("TestCacheGraph1(TestStore1,TestStore2)"), TEXT("")));
	}
}

} // UE::DerivedData

#endif // WITH_LOW_LEVEL_TESTS
