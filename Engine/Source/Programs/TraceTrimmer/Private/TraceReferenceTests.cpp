// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraceReferenceTests.h"

#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "ProfilingDebugging/MiscTrace.h"

// TraceAnalysis
#include "Trace/OutDataStream.h"
#include "Trace/Trace.h"
#include "Trace/Trace.inl"
#include "Trace/TraceWriter.h"
#include "Trace/Analysis.h"

// TraceTrimmer
#include "TraceTrimmer.h"
#include "Misc/Paths.h"
#include "TraceServices/TraceTrimmer.h"

//UE_DISABLE_OPTIMIZATION

namespace UE::Trace::Tests
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class FReferenceFieldWriter
{
public:
	FReferenceFieldWriter(FTraceWriter& InWriter, bool bInUseImportant = false)
		: Writer(InWriter), bUseImportant(bInUseImportant)
	{
		OneSecond = Writer.GetTimeFrequency();
	}

	void DeclareEvents();
	void WriteEvents();

private:
	FTraceWriter& Writer;
	bool bUseImportant;
	uint64 OneSecond;

	uint32 EventId_Def8 = 0;
	uint32 EventId_Def16 = 0;
	uint32 EventId_Def32 = 0;
	uint32 EventId_Def64 = 0;

	uint32 EventId_Ref8 = 0;
	uint32 EventId_Ref16 = 0;
	uint32 EventId_Ref32 = 0;
	uint32 EventId_Ref64 = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FReferencesAnalyzer : public UE::Trace::IAnalyzer
{
public:
	bool ValidateAnalysis(uint32 ExpectedDefinitions, uint32 ExpectedReferences, bool bCheckInvalidReferences = false) const;

	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override
	{
		Context.InterfaceBuilder.RouteAllEvents(0, false);
		Context.InterfaceBuilder.RouteAllEvents(1, true);
		DefinitionCount = 0;
		ReferenceCount = 0;
		InvalidReferences = 0;
		DefinedIds.Empty();
	}

	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override
	{
		const FEventTypeInfo& TypeInfo = Context.EventData.GetTypeInfo();
		const FEventData& EventData = Context.EventData;
		const FEventFieldInfo& FieldInfo = *(TypeInfo.GetFieldInfo(1));
		
		// Check for Definitions.
		if (EnumHasAllFlags(TypeInfo.GetFlags(), EEventFlags::Definition))
		{
			uint64 DefId = EventData.GetValue<uint64>(FieldInfo.GetName());
			DefinedIds.Add(DefId);
			DefinitionCount++;
			return true;
		}

		// Check for References.
		if (FieldInfo.GetType() == FEventFieldInfo::EType::Reference8 ||
			FieldInfo.GetType() == FEventFieldInfo::EType::Reference16 ||
			FieldInfo.GetType() == FEventFieldInfo::EType::Reference32 ||
			FieldInfo.GetType() == FEventFieldInfo::EType::Reference64)
		{
			// Get the reference value and check if analysis has previously seen it.
			uint64 RefValue = EventData.GetValue<uint64>(FieldInfo.GetName());
			if (!DefinedIds.Contains(RefValue))
			{
				InvalidReferences++;
			}
			ReferenceCount++;
		}

		return true;
	}

private:
	uint32 DefinitionCount = 0;
	uint32 ReferenceCount = 0;
	uint32 InvalidReferences = 0;
	TSet<uint64> DefinedIds;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FReferencesAnalyzer::ValidateAnalysis(uint32 ExpectedDefinitions, uint32 ExpectedReferences, bool bCheckInvalidReferences) const
{
	bool bPassed = DefinitionCount == ExpectedDefinitions && ReferenceCount == ExpectedReferences && (InvalidReferences == 0 || !bCheckInvalidReferences);

	LogTraceTrimmerMessage("Results: Definitions=%u (expected %u), References=%u (expected %u), Invalid References=%u",
		DefinitionCount, ExpectedDefinitions, ReferenceCount, ExpectedReferences, InvalidReferences);

	LogTraceTrimmerMessage("%hs", bPassed ? "PASSED" : "FAILED");

	return bPassed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FReferenceFieldWriter::DeclareEvents()
{
	auto DeclareEvent = [&](uint32& DefEventId, uint32& RefEventId, int Size, ETraceWriterFieldType Flag)
	{
		FAnsiString DefName = FAnsiString::Printf("Def_Important_%d", Size);
		FAnsiString RefName = FAnsiString::Printf("Ref_%d", Size);
		const ETraceWriterEventFlags DefFlags = bUseImportant ? ETraceWriterEventFlags::ImportantNoSync : ETraceWriterEventFlags::NoSync;

		DefEventId = Writer.DeclareEvent(ANSITEXTVIEW("ImportantDefinitionsTest"), DefName,
			ETraceWriterEventFlags::Definition | DefFlags)
			.Field(ANSITEXTVIEW("Cycle"), ETraceWriterFieldType::Uint64)
			.DefinitionIdField(ETraceWriterFieldType::DefinitionIdFlag | Flag)
			.Field(ANSITEXTVIEW("Data"), ETraceWriterFieldType::WideString)
			.End();

		RefEventId = Writer.DeclareEvent(ANSITEXTVIEW("ReferencesTest"), RefName,
			ETraceWriterEventFlags::NoSync)
			.Field(ANSITEXTVIEW("Cycle"), ETraceWriterFieldType::Uint64)
			.ReferenceField(ANSITEXTVIEW("Def"), ETraceWriterFieldType::ReferenceFlag | Flag, (uint16)DefEventId)
			.End();
	};
	
	DeclareEvent(EventId_Def8, EventId_Ref8, 8, ETraceWriterFieldType::Uint8);
	DeclareEvent(EventId_Def16, EventId_Ref16, 16, ETraceWriterFieldType::Uint16);
	DeclareEvent(EventId_Def32, EventId_Ref32, 32, ETraceWriterFieldType::Uint32);
	DeclareEvent(EventId_Def64, EventId_Ref64, 64, ETraceWriterFieldType::Uint64);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FReferenceFieldWriter::WriteEvents()
{
	const uint32 GameThreadId = Writer.RegisterThread(ANSITEXTVIEW("GameThread"));
	const uint64 BaseTimestamp = Writer.GetStartTime();

	if (bUseImportant)
	{
		Writer.SetCurrentThreadImportants();
	}
	else
	{
		Writer.SetCurrentThread(GameThreadId);
	}

	auto WriteDefinitionEvent = [&](uint32 DefEventId, uint64 Timestamp, auto Value)
	{
		Writer.WriteEvent(DefEventId)
		  .Field(0, Timestamp)
		  .Field(1, Value)
		  .Field(2, FString::Printf(TEXT("Def_%llu"), Value))
		  .End();
	};

	// Write 20 definitions for each type.
	const uint32 DefCount = 20;
	for (uint32 i = 0; i < DefCount; i++)
	{
		uint64 Timestamp = BaseTimestamp + (i * OneSecond);

		uint8 Def8Value = i;
		WriteDefinitionEvent(EventId_Def8, Timestamp, Def8Value);

		uint16 Def16Value = UINT8_MAX + i;
		WriteDefinitionEvent(EventId_Def16, Timestamp, Def16Value);

		uint32 Def32Value = UINT16_MAX + i;
		WriteDefinitionEvent(EventId_Def32, Timestamp, Def32Value);

		uint64 Def64Value = (uint64)UINT32_MAX + i;
		WriteDefinitionEvent(EventId_Def64, Timestamp, Def64Value);
	}

	// Switch back to GT if we're on Importants Thread.
	Writer.SetCurrentThread(GameThreadId);

	auto WriteReferenceEvent = [&]<typename EventRef>(uint32 RefEventId, uint32 DefEventId, uint64 Timestamp, uint64 RefId)
	{
		EventRef Ref(RefId, DefEventId);
		Writer.WriteEvent(RefEventId)
		  .Field(0, Timestamp)
		  .Field(1, Ref)
		  .End();
	};

	// Write 40 references for each type.
	for (uint32 i = 0; i < 40; i++)
	{
		uint64 Timestamp = BaseTimestamp + (20 * OneSecond) + (i * OneSecond);

		uint8 DefId8 = i % DefCount;
		WriteReferenceEvent.operator()<FEventRef8>(EventId_Ref8, EventId_Def8, Timestamp, DefId8);

		uint16 DefId16 = UINT8_MAX + (i % DefCount);
		WriteReferenceEvent.operator()<FEventRef16>(EventId_Ref16, EventId_Def16, Timestamp, DefId16);

		uint32 DefId32 = UINT16_MAX + (i % DefCount);
		WriteReferenceEvent.operator()<FEventRef32>(EventId_Ref32, EventId_Def32, Timestamp, DefId32);

		uint64 DefId64 = (uint64)UINT32_MAX + (i % DefCount);
		WriteReferenceEvent.operator()<FEventRef64>(EventId_Ref64, EventId_Def64, Timestamp, DefId64);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static void WriteTest(FString TraceFile, bool bUseImportant)
{
	// Generate Trace.
	FFileOutDataStream WriteStream;
	if (!WriteStream.Open(*(TraceFile + TEXT(".utrace"))))
	{
		LogTraceTrimmerError("Failed to create trace file!");
		return;
	}
	
	FTraceWriter Writer(WriteStream);
	Writer.Begin();
	FReferenceFieldWriter ReferenceFieldWriter(Writer, bUseImportant);
	ReferenceFieldWriter.DeclareEvents();
	ReferenceFieldWriter.WriteEvents();
	Writer.End();
	
	// Check for any errors.
	while (true)
	{
		FString Err = Writer.GetLastError();
		if (Err.IsEmpty())
		{
			break;
		}
		LogTraceTrimmerError("%s", *Err);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static FString TrimTraceFile(FString TraceFile, const TCHAR* Suffix, double Start, double End)
{
	FFileDataStream UntrimmedStream;
	if (!UntrimmedStream.Open(*(TraceFile + ".utrace")))
	{
		LogTraceTrimmerError("Failed to open trace for trimming!");
		return FString();
	}

	FString TrimmedFile = FString(TraceFile) + FString(Suffix) + TEXT(".utrace");
	FFileOutDataStream TrimmedStream;
	if (!TrimmedStream.Open(*TrimmedFile))
	{
		LogTraceTrimmerError("Failed to create trimmed trace file!");
		return FString();
	}

	TraceServices::FTrimParameters TrimParams;
	TrimParams.StartTime = Start;
	TrimParams.EndTime = End;

	TraceServices::Trim(TrimParams, UntrimmedStream, TrimmedStream);
	return TrimmedFile;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static void ValidateTrimmedFile(FReferencesAnalyzer& Analyzer, const FString& TrimmedPath, 
	uint32 ExpectedDefinitions, uint32 ExpectedReferences, bool bCheckInvalidReferences = false)
{
	FFileDataStream ReadStream;
	if (!ReadStream.Open(*TrimmedPath))
	{
		LogTraceTrimmerError("Failed to open trimmed trace!");
		return;
	}

	FAnalysisContext Context;
	Context.AddAnalyzer(Analyzer);
	Context.Process(ReadStream).Wait();
	Analyzer.ValidateAnalysis(ExpectedDefinitions, ExpectedReferences, bCheckInvalidReferences);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void TraceTestReferences(const TCHAR* Directory)
{
	LogTraceTrimmerMessage("\nStarting Reference Fields Test\n");

	const uint64 TestStartTime = FPlatformTime::Cycles64();

	FString TraceFile = FString(Directory) / TEXT("DefRefTest");
	LogTraceTrimmerMessage("Writing %s...", *TraceFile);
	WriteTest(TraceFile, false);

	FString TraceFile2 = FString(Directory) / TEXT("DefRefTest2");
	LogTraceTrimmerMessage("Writing %s...", *TraceFile2);
	WriteTest(TraceFile2, true);

	FReferencesAnalyzer Analyzer;

	// Test 1: Full trace.
	FString TrimmedPath = TrimTraceFile(TraceFile, TEXT("T1"), 0.0, 60.0);
	if (!TrimmedPath.IsEmpty())
	{
		ValidateTrimmedFile(
			Analyzer,
			TrimmedPath,
			80,							// Expected Definitions
			160,						// Expected References
			true);	// Check for invalid References?
	}

	// Test 2: All Defs, with most Refs.
	TrimmedPath = TrimTraceFile(TraceFile, TEXT("T2"), 0.0, 40.0);
	if (!TrimmedPath.IsEmpty())
	{
		ValidateTrimmedFile(
			Analyzer,
			TrimmedPath,
			80,
			84,
			true);
	}

	// Test 3: Refs only, large window.
	TrimmedPath = TrimTraceFile(TraceFile, TEXT("T3"), 20.0, 30.0);
	if (!TrimmedPath.IsEmpty())
	{
		ValidateTrimmedFile(
			Analyzer,
			TrimmedPath,
			0,
			44,
			false);
	}

	// Test 4: Defs only.
	TrimmedPath = TrimTraceFile(TraceFile, TEXT("T4"), 0.0, 19.0);
	if (!TrimmedPath.IsEmpty())
	{
		ValidateTrimmedFile(
			Analyzer,
			TrimmedPath,
			80,
			0,
			true);
	}

	// Test 5: All Refs.
	TrimmedPath = TrimTraceFile(TraceFile, TEXT("T5"), 20.0, 60.0);
	if (!TrimmedPath.IsEmpty())
	{
		ValidateTrimmedFile(
			Analyzer,
			TrimmedPath,
			0,
			160,
			false);
	}

	// Test 6: Defs and Refs, at a shared boundary.
	TrimmedPath = TrimTraceFile(TraceFile, TEXT("T6"), 10.0, 40.0);
	if (!TrimmedPath.IsEmpty())
	{
		ValidateTrimmedFile(
			Analyzer,
			TrimmedPath,
			40,
			84,
			false);
	}

	// Test 7: Same boundary as T6 but with Important Definitions.
	TrimmedPath = TrimTraceFile(TraceFile2, TEXT("T7"), 10.0, 40.0);
	if (!TrimmedPath.IsEmpty())
	{
		ValidateTrimmedFile(
			Analyzer,
			TrimmedPath,
			80,
			84,
			true);
	}

	const uint64 TestDuration = FPlatformTime::Cycles64() - TestStartTime;
	const double TestDurationSeconds = double(TestDuration) * FPlatformTime::GetSecondsPerCycle64();
	LogTraceTrimmerMessage("\nCompleted Reference Fields Test (%.6f seconds)", TestDurationSeconds);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Trace::Tests

//UE_ENABLE_OPTIMIZATION
