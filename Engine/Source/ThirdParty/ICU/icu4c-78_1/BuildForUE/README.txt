=== BUILDING ICU ===
This is a modified version of the ICU source tree that has the following changes:
  - UProperty has been renamed to UCharProperty to avoid conflicts with the Unreal version of UProperty.
  - The u_setDataFileFunctions function has been added to allow ICU data to be loaded with the Unreal Filesystem (see urename.h, uclean.h, and umapfile.cpp).
  - The U_PLATFORM_HAS_GETENV macro has been added which can be defined to 0 in platform build scripts to disable code that calls getenv() for platforms that don't have this function.
  - The __cpp_char8_t guard in ucol.h and coll.h has been changed to __cpp_lib_char8_t to correctly condition std::u8string_view operator overloads on standard library support rather than compiler support.

These changes must be reapplied to any new versions of ICU, then, the header files found in source/common/unicode and source/i18n/unicode should be copied to include/unicode.

The CMakeLists.txt file found in this directory is able to build the minimal subset of ICU that Unreal uses, and should be preferred over the existing Makefiles.
The only exception to this is if you need to generate ICU data, as that requires a tool not compiled by our CMake file.

=== BUILDING DATA ===
We build several sets of ICU data that needs to be copied into Engine/Content/Internationalization.
The Data/BuildICUData.bat file can generate these data sets using the filters found in Data/Filters.

Note: Building ICU data requires Python 3 and Visual Studio to be installed and available on your PATH.

 1) Copy the icu4c-78_1 folder to a short path, like D:\icu78.
      Note: The script builds source/allinone/allinone.sln, which generates build output throughout
      the source tree (bin64/, lib64/, include/, etc.). A separate copy prevents depot pollution
      and avoids MAX_PATH failures.
 2) Optionally, download the ICU 78 data zip and extract it over source/data to use a different data release.
 3) Run BuildForUE/Data/BuildICUData.bat from the copied location.
 4) Copy the built ICU data from icu-data to Engine/Content/Internationalization (these are used by packaged projects).
 5) Copy the ICU data folder from the "All" data set to directly under Engine/Content/Internationalization (this is used by the editor).
 6) For each target platform, update ICUDataVersion in the platform's DataDrivenPlatformInfo.ini to reference the new data folder name so the correct set is staged and packaged during a cook.
      e.g. Engine/Config/Windows/DataDrivenPlatformInfo.ini: ICUDataVersion=icudt78l

=== DATA FILTER NOTES ===

All filter presets exclude transliteration data ("translit": {"filterType": "exclude"}).
This is safe because UE compiles ICU with UCONFIG_NO_TRANSLITERATION=1 (set in both
CMakeLists.txt and ICU.Build.cs), which disables transliteration APIs at the code level.
The translit .res files would never be loaded. Excluding them saves ~1.1 MB on the All preset.

=== UPDATING TIMEZONE DATA (adapted from https://unicode-org.github.io/icu/userguide/datetime/timezone/#icu4c-tz-update-with-drop-in-res-files-icu-54-and-newer) ===
The ICU timezone data from IANA (colloquially, tzdata) gets updated much more frequently than the monolithic CLDR data embedded in ICU.
To update this data in engine content in-place (for icu4c versions > 54.0):

1) Sync the latest tzdata from the icu-data repo (the most recent folder in https://github.com/unicode-org/icu-data/tree/master/tzdata/icunew)
	Note: we are specifically looking at the "44" (for ICU 4.4) folder inside of here.
2) Replace "metaZones.res", "timezoneTypes.res", "windowsZones.res", and "zoneinfo64.res" in each of the filtered data directories in Engine/Content/Internationalization (including the base version the editor uses) for your icu4c version with the copies from the repo's "le" (little-endian) folder.
	Note: these binary files can be used directly (as long as icu4c 54.0 or greater is being used) and do not need to be regenerated or filtered; each filtered content set receives the whole resource.

	ex:
		Engine/Content/Internationalization/All/icudt78l/(metaZones|timezoneTypes|windowsZones|zoneinfo64).res
		Engine/Content/Internationalization/icudt78l/(metaZones|timezoneTypes|windowsZones|zoneinfo64).res
		et al.

=== APPLIED PATCHES ===

Patch 1: UProperty -> UCharProperty Rename
  source/common/unicode/uchar.h
  source/common/unicode/uset.h
  source/common/unicode/uniset.h
  include/unicode/uchar.h
  include/unicode/uset.h
  include/unicode/uniset.h
  source/common/uprops.h
  source/common/ucase.h
  source/common/ubidi_props.h
  source/common/propname.h
  source/common/emojiprops.h
  source/common/uprops.cpp
  source/common/ucase.cpp
  source/common/ubidi_props.cpp
  source/common/propname.cpp
  source/common/emojiprops.cpp
  source/common/uset_props.cpp
  source/common/uniset_props.cpp
  source/common/characterproperties.cpp
  source/tools/toolutil/ppucd.h
  source/tools/icuexportdata/icuexportdata.cpp
  source/test/intltest/ucdtest.cpp
  source/test/fuzzer/uprop_fuzzer.cpp
  source/test/cintltst/cucdtst.c

Patch 2: u_setDataFileFunctions (custom VFS data loading)
  source/common/unicode/uclean.h
  source/common/unicode/urename.h
  source/common/umapfile.cpp
  include/unicode/uclean.h
  include/unicode/urename.h

  ICU 78 signature change: UDataFileOpenFn now includes an int32_t* outLength parameter.
  ICU 78 added data integrity checks in ucnv_io.cpp (initAliasData) that call udata_getLength()
  to validate the size of loaded data files. udata_getLength() requires UDataMemory::length to
  be set, but uprv_mapFileFromCallback (our VFS bridge) never populated it — the native code
  paths (MapViewOfFile on Windows, mmap on POSIX) set length from the OS file size, but the
  callback path had no way to pass it through. ICU 64 did not have these length checks, so the
  missing length was harmless. In ICU 78, udata_getLength() returns -1 when length is unset,
  which causes initAliasData to fail with U_INVALID_FORMAT_ERROR at u_init() time.

  The fix adds int32_t* outLength to UDataFileOpenFn so the caller (UE's OpenDataFile) can
  report the file size. uprv_mapFileFromCallback then sets pData->length from the callback's
  output, matching the behavior of the native file mapping paths.

  This is an ICU 78-only change. ICU 64's uclean.h and umapfile.cpp retain the original
  4-parameter UDataFileOpenFn signature. UE's OpenDataFile in ICUInternationalization.cpp
  is version-gated with #if WITH_ICU_V78 to compile against the correct signature.

Patch 3: U_PLATFORM_HAS_GETENV guard (console platform getenv support)
  source/common/putil.cpp
  source/i18n/japancal.cpp

Patch 4: __cpp_lib_char8_t guard for u8string_view operators
  source/i18n/unicode/ucol.h
  source/i18n/unicode/coll.h
  include/unicode/ucol.h
  include/unicode/coll.h

  ICU 78 added operator overloads accepting std::u8string_view to ucol.h and coll.h, guarded
  by #if defined(__cpp_char8_t). __cpp_char8_t is the compiler language feature macro — it is
  defined whenever the compiler recognises char8_t as a distinct type (i.e. in C++20 mode).
  However, std::u8string_view is a standard library type, not a compiler type. On platforms
  that ship an older or partial C++20 standard library, char8_t is a valid keyword and
  __cpp_char8_t is defined, but the library's <string_view> does not provide std::u8string_view.
  In those cases the original guard passes, the code attempts to use a type that doesn't exist,
  and compilation fails.

  The correct guard is __cpp_lib_char8_t, the standard library feature test macro defined to
  201907L when the library provides std::u8string_view and the rest of the char8_t library
  support. This is the same pattern already used elsewhere in ICU (e.g. stringpiece.h) to
  distinguish compiler-level char8_t support from library-level char8_t support.

  Changed both guards from __cpp_char8_t to __cpp_lib_char8_t. Platforms with a full C++20
  standard library define __cpp_lib_char8_t and are unaffected (e.g Windows,, Linux,, Android)
  the operators compile identically. Platforms where the standard library predates or omits
  std::u8string_view will have __cpp_lib_char8_t undefined, causing the operators to be
  compiled out cleanly with no impact on the rest of ICU.