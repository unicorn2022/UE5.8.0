// Copyright Epic Games, Inc. All Rights Reserved.

#include "DSP/Delay.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace Audio::Tests
{
	static constexpr float DelayTestSampleRate = 100.0f;

	// ------------------------------------------------------------------ helpers

	static TArray<float> RunDelayOnImpulse(FDelay& Delay, int32 NumSamples)
	{
		TArray<float> Outputs;
		Outputs.SetNumZeroed(NumSamples);
		Outputs[0] = Delay.ProcessAudioSample(1.0f);
		for (int32 i = 1; i < NumSamples; ++i)
		{
			Outputs[i] = Delay.ProcessAudioSample(0.0f);
		}
		return Outputs;
	}

	// ================================================================== Tests ==

	// Integer delay — impulse must appear unmodified exactly N samples later.
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FDelayIntegerDelay,
		"System.SignalProcessing.Delay.IntegerDelay",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	bool FDelayIntegerDelay::RunTest(const FString&)
	{
		FDelay Delay;
		Delay.Init(DelayTestSampleRate, 0.5f);
		Delay.SetDelaySamples(2.0f);

		const TArray<float> Out = RunDelayOnImpulse(Delay, 5);

		UTEST_EQUAL("2-sample delay: output[0] == 0", Out[0], 0.0f);
		UTEST_EQUAL("2-sample delay: output[1] == 0", Out[1], 0.0f);
		UTEST_EQUAL("2-sample delay: output[2] == 1", Out[2], 1.0f);
		UTEST_EQUAL("2-sample delay: output[3] == 0", Out[3], 0.0f);
		return true;
	}

	// Fractional delay — impulse energy is split across adjacent samples via linear interpolation.
	// With delay=1.25: Fraction=0.25, Read() = Lerp(Yn, YnPrev, 0.25).
	//   output[1] = Lerp(buf[WriteIndex-1]=1.0, buf[WriteIndex-2]=0.0, 0.25) = 0.75
	//   output[2] = Lerp(buf[WriteIndex-1]=0.0, buf[WriteIndex-2]=1.0, 0.25) = 0.25
	// Energy is preserved (0.75+0.25=1.0) and centroid lands at 1*0.75 + 2*0.25 = 1.25 samples.
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FDelayFractionalDelayInterpolation,
		"System.SignalProcessing.Delay.FractionalDelayInterpolation",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	bool FDelayFractionalDelayInterpolation::RunTest(const FString&)
	{
		FDelay Delay;
		Delay.Init(DelayTestSampleRate, 0.5f);
		Delay.SetDelaySamples(1.25f);

		const TArray<float> Out = RunDelayOnImpulse(Delay, 5);

		UTEST_EQUAL("1.25-sample delay: output[0] == 0", Out[0], 0.0f);
		UTEST_TRUE("1.25-sample delay: output[1] == 0.75", FMath::IsNearlyEqual(Out[1], 0.75f, 1e-6f));
		UTEST_TRUE("1.25-sample delay: output[2] == 0.25", FMath::IsNearlyEqual(Out[2], 0.25f, 1e-6f));
		UTEST_EQUAL("1.25-sample delay: output[3] == 0", Out[3], 0.0f);

		const float Energy = Out[1] + Out[2];
		UTEST_TRUE("1.25-sample delay: total impulse energy preserved", FMath::IsNearlyEqual(Energy, 1.0f, 1e-6f));
		return true;
	}

	// Sub-1-sample fractional delay — the write-before-read ordering ensures ReadIndex==WriteIndex
	// correctly interpolates between the current input and the last written sample.
	// With delay=0.5: output[0] = Lerp(1.0, 0.0, 0.5) = 0.5, output[1] = Lerp(0.0, 1.0, 0.5) = 0.5.
	// Energy is preserved and centroid lands at 0*0.5 + 1*0.5 = 0.5 samples.
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FDelaySubOneSampleDelay,
		"System.SignalProcessing.Delay.SubOneSampleDelay",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	bool FDelaySubOneSampleDelay::RunTest(const FString&)
	{
		FDelay Delay;
		Delay.Init(DelayTestSampleRate, 0.5f);
		Delay.SetDelaySamples(0.5f);

		const TArray<float> Out = RunDelayOnImpulse(Delay, 5);

		UTEST_TRUE("0.5-sample delay: output[0] == 0.5", FMath::IsNearlyEqual(Out[0], 0.5f, 1e-6f));
		UTEST_TRUE("0.5-sample delay: output[1] == 0.5", FMath::IsNearlyEqual(Out[1], 0.5f, 1e-6f));
		UTEST_EQUAL("0.5-sample delay: output[2] == 0",  Out[2], 0.0f);

		const float Energy = Out[0] + Out[1];
		UTEST_TRUE("0.5-sample delay: impulse energy preserved", FMath::IsNearlyEqual(Energy, 1.0f, 1e-6f));

		const float Centroid = 0.f * Out[0] + 1.f * Out[1];
		UTEST_TRUE("0.5-sample delay: energy centroid at 0.5 samples", FMath::IsNearlyEqual(Centroid, 0.5f, 1e-6f));
		return true;
	}

	// Zero delay — output must equal input with no latency.
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FDelayZeroDelay,
		"System.SignalProcessing.Delay.ZeroDelay",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	bool FDelayZeroDelay::RunTest(const FString&)
	{
		FDelay Delay;
		Delay.Init(DelayTestSampleRate, 0.5f);
		Delay.SetDelaySamples(0.0f);

		UTEST_TRUE("Zero delay: output[0] == 0.5",  FMath::IsNearlyEqual(Delay.ProcessAudioSample( 0.5f),  0.5f, 1e-6f));
		UTEST_TRUE("Zero delay: output[1] == -0.3", FMath::IsNearlyEqual(Delay.ProcessAudioSample(-0.3f), -0.3f, 1e-6f));
		return true;
	}

	// Reset clears the buffer — output must be zero immediately after Reset().
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FDelayResetClearsState,
		"System.SignalProcessing.Delay.ResetClearsState",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	bool FDelayResetClearsState::RunTest(const FString&)
	{
		FDelay Delay;
		Delay.Init(DelayTestSampleRate, 0.5f);
		Delay.SetDelaySamples(2.0f);

		for (int32 i = 0; i < 10; ++i)
		{
			Delay.ProcessAudioSample(1.0f);
		}

		Delay.Reset();

		UTEST_EQUAL("Output is 0 on first sample after Reset()", Delay.ProcessAudioSample(0.0f), 0.0f);
		return true;
	}

	// ResetWithFade — after priming the delay with a non-zero signal, the output must decay to
	// silence once the fade period and remaining delay have drained.
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FDelayResetWithFade,
		"System.SignalProcessing.Delay.ResetWithFade",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	bool FDelayResetWithFade::RunTest(const FString&)
	{
		FDelay Delay;
		Delay.Init(DelayTestSampleRate, 2.0f);
		Delay.SetDelaySamples(10.0f);

		// Prime the buffer with a non-zero signal
		for (int32 i = 0; i < 50; ++i)
		{
			Delay.ProcessAudioSample(1.0f);
		}

		Delay.ResetWithFade();

		// Drain silence through fade length (64) + delay (10) + margin
		constexpr int32 DrainSamples = 64 + 10 + 10;
		for (int32 i = 0; i < DrainSamples; ++i)
		{
			Delay.ProcessAudioSample(0.0f);
		}

		// Buffer and fade should both be fully exhausted; expect silence
		UTEST_TRUE("ResetWithFade: output is silent after fade and delay drain",
			FMath::IsNearlyZero(Delay.ProcessAudioSample(0.0f), 1e-6f));
		return true;
	}

	// SetEasedDelayMsec with bIsInit=true must produce output identical to SetDelaySamples
	// for the equivalent delay, since for in-range values it resolves to an immediate set.
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FDelaySetEasedInitMatchesSetDelaySamples,
		"System.SignalProcessing.Delay.SetEasedInitMatchesSetDelaySamples",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	bool FDelaySetEasedInitMatchesSetDelaySamples::RunTest(const FString&)
	{
		// 20ms at 100Hz = 2 samples
		constexpr float TargetDelayMsec    = 20.0f;
		constexpr float TargetDelaySamples = 2.0f;
		constexpr int32 NumSamples         = 6;

		FDelay DelayA;
		DelayA.Init(DelayTestSampleRate, 0.5f);
		DelayA.SetEasedDelayMsec(TargetDelayMsec, /*bIsInit=*/true);

		FDelay DelayB;
		DelayB.Init(DelayTestSampleRate, 0.5f);
		DelayB.SetDelaySamples(TargetDelaySamples);

		const TArray<float> OutA = RunDelayOnImpulse(DelayA, NumSamples);
		const TArray<float> OutB = RunDelayOnImpulse(DelayB, NumSamples);

		for (int32 i = 0; i < NumSamples; ++i)
		{
			if (!FMath::IsNearlyEqual(OutA[i], OutB[i], 1e-6f))
			{
				AddError(FString::Printf(
					TEXT("SetEasedDelayMsec(init) mismatch at sample %d: eased=%.8f direct=%.8f"),
					i, OutA[i], OutB[i]));
				return false;
			}
		}
		return true;
	}

	// ReadDelayAt — must return the correct interpolated value at an arbitrary time offset
	// relative to the current write position.
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FDelayReadDelayAt,
		"System.SignalProcessing.Delay.ReadDelayAt",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	bool FDelayReadDelayAt::RunTest(const FString&)
	{
		FDelay Delay;
		Delay.Init(DelayTestSampleRate, 0.5f);
		Delay.SetDelaySamples(2.0f);

		// Process impulse + 2 zeros. After these 3 ticks WriteIndex==3 and the impulse
		// sits at buf[0], so ReadDelayAt(30ms) = 3 samples back = buf[0] = 1.0.
		Delay.ProcessAudioSample(1.0f);
		Delay.ProcessAudioSample(0.0f);
		Delay.ProcessAudioSample(0.0f);

		// Integer offset: 3 samples = 30ms at 100Hz
		UTEST_TRUE("ReadDelayAt(30ms) == 1.0",
			FMath::IsNearlyEqual(Delay.ReadDelayAt(30.0f), 1.0f, 1e-6f));

		// Integer offset that misses the impulse
		UTEST_TRUE("ReadDelayAt(20ms) == 0.0",
			FMath::IsNearlyEqual(Delay.ReadDelayAt(20.0f), 0.0f, 1e-6f));

		// Fractional offset straddling the impulse: 2.5 samples = 25ms
		// ReadAtIndex = 3-2 = 1 (buf[1]=0), prev = buf[0]=1.0, Fraction=0.5 => 0.5
		UTEST_TRUE("ReadDelayAt(25ms) == 0.5",
			FMath::IsNearlyEqual(Delay.ReadDelayAt(25.0f), 0.5f, 1e-6f));

		return true;
	}

	// Circular buffer wrap-around — impulse response must be identical before and after the
	// write index wraps, with no spurious output at any sample index other than the expected one.
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FDelayCircularBufferWrapAround,
		"System.SignalProcessing.Delay.CircularBufferWrapAround",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	bool FDelayCircularBufferWrapAround::RunTest(const FString&)
	{
		FDelay Delay;
		Delay.Init(DelayTestSampleRate, 0.5f);
		Delay.SetDelaySamples(3.0f);

		// 100 samples well exceeds the ~51-sample buffer, forcing multiple wrap-arounds
		constexpr int32 NumSamples = 100;
		const TArray<float> Out = RunDelayOnImpulse(Delay, NumSamples);

		UTEST_EQUAL("Wrap: output[3] == 1.0", Out[3], 1.0f);

		for (int32 i = 0; i < NumSamples; ++i)
		{
			if (i == 3)
			{
				continue;
			}
			if (!FMath::IsNearlyZero(Out[i], 1e-6f))
			{
				AddError(FString::Printf(TEXT("Wrap: spurious output %.8f at sample %d"), Out[i], i));
				return false;
			}
		}
		return true;
	}

	// Output attenuation — SetOutputAttenuationDB must scale output by the correct linear factor.
	// -20dB = Pow(10, -1) = 0.1 exactly.
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FDelayOutputAttenuation,
		"System.SignalProcessing.Delay.OutputAttenuation",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	bool FDelayOutputAttenuation::RunTest(const FString&)
	{
		FDelay Delay;
		Delay.Init(DelayTestSampleRate, 0.5f);
		Delay.SetDelaySamples(1.0f);
		Delay.SetOutputAttenuationDB(-20.0f);

		const TArray<float> Out = RunDelayOnImpulse(Delay, 4);

		UTEST_EQUAL("Attenuation: output[0] == 0",   Out[0], 0.0f);
		UTEST_TRUE("Attenuation: output[1] == 0.1",  FMath::IsNearlyEqual(Out[1], 0.1f, 1e-6f));
		UTEST_EQUAL("Attenuation: output[2] == 0",   Out[2], 0.0f);
		return true;
	}

	// ProcessAudioBuffer must produce sample-identical output to a ProcessAudioSample loop.
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FDelayProcessBufferMatchesSampleLoop,
		"System.SignalProcessing.Delay.ProcessBufferMatchesSampleLoop",
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	bool FDelayProcessBufferMatchesSampleLoop::RunTest(const FString&)
	{
		constexpr int32 NumSamples = 64;

		TArray<float> Input;
		Input.SetNumUninitialized(NumSamples);
		for (int32 i = 0; i < NumSamples; ++i)
		{
			Input[i] = ((i % 7) - 3) * 0.1f;
		}

		FDelay DelayA;
		DelayA.Init(DelayTestSampleRate, 0.5f);
		DelayA.SetDelaySamples(1.25f);
		TArray<float> OutputA;
		OutputA.SetNumZeroed(NumSamples);
		DelayA.ProcessAudioBuffer(Input.GetData(), NumSamples, OutputA.GetData());

		FDelay DelayB;
		DelayB.Init(DelayTestSampleRate, 0.5f);
		DelayB.SetDelaySamples(1.25f);
		TArray<float> OutputB;
		OutputB.SetNumZeroed(NumSamples);
		for (int32 i = 0; i < NumSamples; ++i)
		{
			OutputB[i] = DelayB.ProcessAudioSample(Input[i]);
		}

		for (int32 i = 0; i < NumSamples; ++i)
		{
			if (!FMath::IsNearlyEqual(OutputA[i], OutputB[i], 1e-6f))
			{
				AddError(FString::Printf(TEXT("Output mismatch at sample %d: buffer=%.8f sample=%.8f"), i, OutputA[i], OutputB[i]));
				return false;
			}
		}
		return true;
	}

} // namespace Audio::Tests

#endif // WITH_DEV_AUTOMATION_TESTS
