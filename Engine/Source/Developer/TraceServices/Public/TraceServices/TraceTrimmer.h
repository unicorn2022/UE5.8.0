// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/AnsiString.h"
#include "Templates/UniquePtr.h"

// TraceAnalysis
#include "Trace/Analysis.h"
#include "Trace/DataStream.h"
#include "Trace/OutDataStream.h"

#include <limits>

#define UE_API TRACESERVICES_API

namespace TraceServices {

struct FTrimParameters
{
	/**
	 * The start time filter.
	 * The analysis will ignore non-important events with timestamp < StartTime.
	 */
	double StartTime = -std::numeric_limits<double>::infinity();

	/**
	 * The end time filter.
	 * The analysis will stop when a first non-important event with timestamp > EndTime is detected.
	 */
	double EndTime = +std::numeric_limits<double>::infinity();

	/**
	 * Comma separated list of trace events to include.
	 * The events are specified as "logger.event" wildcard patterns.
	 * Example: "$trace.*,cpu.*".
	 * If specified, only the trace events with name that matches one of the patterns in the Include string will be included.
	 * Empty string (default) is equivalent with "*.*" (include all).
	 */
	FAnsiString Include;

	/**
	 * Comma separated list of trace events to exclude.
	 * The events are specified as "logger.event" wildcard patterns.
	 * Example: "$trace.*,cpu.*".
	 * If specified, the trace events with name that matches one of the pattern in the Exclude string will be excluded.
	 */
	FAnsiString Exclude;
};

class ITrimAnalysisProcessor
{
public:
	ITrimAnalysisProcessor() = default;
	virtual ~ITrimAnalysisProcessor() = default;

	/** Checks if this object instance is valid and currently processing */
	virtual bool IsActive() const = 0;

	/** End processing a trace stream. */
	virtual void Stop() = 0;

	/** Wait for the entire stream to have been processed and analyzed. */
	virtual void Wait() = 0;

	/** Pause or resume the processing.
	 * @param bState Pause if true, resume if false. */
	virtual void Pause(bool bState) = 0;
};

/**
 * Trims a trace stream using the provided parameters.
 * The function starts the trimming process and returns immediately a unique pointer
 * to the trim analysis processor object.
 * The caller is responsible for waiting the processor to complete.
 *
 * @param Parameters The trim process parameters
 * @param InputDataStream The input data stream
 * @param OutputDataStream The output data stream
 * @returns a unique pointer to the trim analysis processor object
 */
UE_API TUniquePtr<ITrimAnalysisProcessor> TrimAsync(FTrimParameters& Parameters, UE::Trace::IInDataStream& InputDataStream, UE::Trace::IOutDataStream& OutputDataStream);

/**
 * Trims a trace stream using the provided parameters.
 * The function waits for the trimming process to complete, before returning.
 *
 * @param Parameters The trim process parameters
 * @param InputDataStream The input data stream
 * @param OutputDataStream The output data stream
 * @returns 0 if the trim process is completed successfully, non-zero otherwise
 */
UE_API int32 Trim(FTrimParameters& Parameters, UE::Trace::IInDataStream& InputDataStream, UE::Trace::IOutDataStream& OutputDataStream);

} // namespace TraceServices

#undef UE_API
