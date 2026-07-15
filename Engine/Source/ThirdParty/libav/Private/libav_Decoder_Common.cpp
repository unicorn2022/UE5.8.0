// Copyright Epic Games, Inc. All Rights Reserved.

#include "libav_Decoder_Common.h"

#include <atomic>

/***************************************************************************************************************************************************/

DEFINE_LOG_CATEGORY(LogLibAV);

/***************************************************************************************************************************************************/


#ifndef WITH_LIBAV
#define WITH_LIBAV 0
#endif

#if WITH_LIBAV
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}
#endif

namespace FLibavDecoderInternal
{
    static std::atomic<int32> InitCount(0);
	static bool bLibCodecUsable = false;
	static bool bLibUtilUsable = false;

	static void ShowBuildInfo()
	{
		UE_LOGF(LogLibAV, Log, "libav was not built any codec support. Please see the README in the libav folder and rebuild it.");
	}
}

void ILibavDecoder::Startup()
{
	if (++FLibavDecoderInternal::InitCount == 1)
	{
#if WITH_LIBAV
		uint32 VersionCodec = avcodec_version();
		FString BuildConfig(avcodec_configuration());
		FString License(avcodec_license());
		FLibavDecoderInternal::bLibCodecUsable = AV_VERSION_MAJOR(VersionCodec) == LIBAVCODEC_VERSION_MAJOR && AV_VERSION_MINOR(VersionCodec) == LIBAVCODEC_VERSION_MINOR;

		UE_LOGF(LogLibAV, Log, "Using libavcodec version %u.%u.%u, compiled with header version %u.%u.%u", AV_VERSION_MAJOR(VersionCodec), AV_VERSION_MINOR(VersionCodec), AV_VERSION_MICRO(VersionCodec), LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO);
		UE_LOGF(LogLibAV, Verbose, "libavcodec license: %ls", *License);
		UE_LOGF(LogLibAV, VeryVerbose, "libavcodec configuration: %ls", *BuildConfig);

		// Same for libavutil
		uint32 VersionUtil = avutil_version();
		BuildConfig = avutil_configuration();
		License = avutil_license();
		UE_LOGF(LogLibAV, Log, "Using libavutil version %u.%u.%u, compiled with header version %u.%u.%u", AV_VERSION_MAJOR(VersionUtil), AV_VERSION_MINOR(VersionUtil), AV_VERSION_MICRO(VersionUtil), LIBAVUTIL_VERSION_MAJOR, LIBAVUTIL_VERSION_MINOR, LIBAVUTIL_VERSION_MICRO);
		UE_LOGF(LogLibAV, Verbose, "libavutil license: %ls", *License);
		UE_LOGF(LogLibAV, VeryVerbose, "libavutil configuration: %ls", *BuildConfig);
		FLibavDecoderInternal::bLibUtilUsable = AV_VERSION_MAJOR(VersionUtil) == LIBAVUTIL_VERSION_MAJOR && AV_VERSION_MINOR(VersionUtil) == LIBAVUTIL_VERSION_MINOR;

		// Compatibility check
		if (!FLibavDecoderInternal::bLibCodecUsable || !FLibavDecoderInternal::bLibUtilUsable)
		{
			UE_LOGF(LogLibAV, Warning, "Warning: Installed libavcodec and/or libavutil are not compatible with the headers this plugin has been compiled with");
			UE_LOGF(LogLibAV, Warning, "  libavcodec version %u.%u.%u, compiled with header version %u.%u.%u", AV_VERSION_MAJOR(VersionCodec), AV_VERSION_MINOR(VersionCodec), AV_VERSION_MICRO(VersionCodec), LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO);
			UE_LOGF(LogLibAV, Warning, "   libavutil version %u.%u.%u, compiled with header version %u.%u.%u", AV_VERSION_MAJOR(VersionUtil), AV_VERSION_MINOR(VersionUtil), AV_VERSION_MICRO(VersionUtil), LIBAVUTIL_VERSION_MAJOR, LIBAVUTIL_VERSION_MINOR, LIBAVUTIL_VERSION_MICRO);
			UE_LOGF(LogLibAV, Warning, "libav will not be used due to avoid issues. Please rebuild the plugin with the correct versions!");
		}

#else
		FLibavDecoderInternal::ShowBuildInfo();
#endif
	}
}

void ILibavDecoder::Shutdown()
{
	--FLibavDecoderInternal::InitCount;
}


bool ILibavDecoder::IsLibAvAvailable()
{
	return !!WITH_LIBAV && FLibavDecoderInternal::bLibCodecUsable && FLibavDecoderInternal::bLibUtilUsable;
}

void ILibavDecoder::LogLibraryNeeded()
{
#if WITH_LIBAV == 0
	FLibavDecoderInternal::ShowBuildInfo();
#endif
}
