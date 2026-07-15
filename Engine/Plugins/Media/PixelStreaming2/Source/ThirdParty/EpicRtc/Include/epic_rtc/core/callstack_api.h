// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

//
// Interface to callstack system used for memory tracking and profiling
//

class EpicRtcMemoryInterface;
class EpicRtcStringInterface;

// Flags to identify properties or desired information aquisition for the scope (eg it's a known leak, or record cpu profiling info)
enum class EpicRtcScopeFlag : uint8_t
{
    NONE        = 0,
    DEBUG       = 1 << 0,   // Scope requires investigation for possible leaks
    KNOWN_LEAK  = 1 << 1,   // Scope has known unfixable leaks (normally just static allocations freed at main exits)
    LITERAL     = 1 << 2,   // Scope uses the flag-specific string as it's name
    CPUPROFILE  = 1 << 3,   // Scope is used for cpu profiling
    REPORTED    = 1 << 4,   // Scope has been reported already (used by MemoryTracker to prevent double-reporting of leaks)
};

enum class EpicRtcCallstackType : uint8_t
{
    NONE        = 0,
    MEMORY      = 1 << 0,   // Memory tracking
    CPUPROFILE  = 1 << 1,   // Cpu profiling
};

enum class EpicRtcTagFlag : uint8_t
{
    NONE            = 0,
    REPORTED        = 1 << 0,   // Stats have been reported already (used by MemoryTracker to prevent double-reporting)
    LOCAL_MEM_STATS = 1 << 1,   // Maintain local memory stats for this tag
};

// Convenience operators for enums
inline EpicRtcScopeFlag operator|(EpicRtcScopeFlag a, EpicRtcScopeFlag b)             { return static_cast<EpicRtcScopeFlag>(static_cast<int>(a) | static_cast<int>(b)); }
inline EpicRtcScopeFlag operator~(EpicRtcScopeFlag a)                                 { return static_cast<EpicRtcScopeFlag>(~static_cast<int>(a)); }
inline EpicRtcTagFlag operator|(EpicRtcTagFlag a, EpicRtcTagFlag b)                   { return static_cast<EpicRtcTagFlag>(static_cast<int>(a) | static_cast<int>(b)); }
inline EpicRtcTagFlag operator~(EpicRtcTagFlag a)                                     { return static_cast<EpicRtcTagFlag>(~static_cast<int>(a)); }
inline EpicRtcCallstackType operator|(EpicRtcCallstackType a, EpicRtcCallstackType b) { return static_cast<EpicRtcCallstackType>(static_cast<int>(a) | static_cast<int>(b)); }
inline int operator&(EpicRtcScopeFlag a, EpicRtcScopeFlag b)                          { return static_cast<int>(a) & static_cast<int>(b); }
inline int operator&(EpicRtcCallstackType a, EpicRtcCallstackType b)                  { return static_cast<int>(a) & static_cast<int>(b); }
inline int operator&(EpicRtcTagFlag a, EpicRtcTagFlag b)                              { return static_cast<int>(a) & static_cast<int>(b); }

class EpicRtcTagInfo
{
public:
    const EpicRtcTagInfo*  _next;                // All tags are in a global list
    const char*            _name;                // Name for the tag (scope name when tag in use)
    void*                  _localStaticStorage;  // For use by external memory tracker or OnEntry/OnExit callbacks to retain state
    mutable EpicRtcTagFlag _flags;               // Flags for memory tracker to respond to
};

class EpicRtcCallstackInterface
{
    public:

    // Optional extry/exit functions used to hook into Unreal LLM
    inline static constexpr uint32_t LOCALSTACK_BYTES = 16; // Amount of stackspace reserved for Entry/Exit functions to hold state for their scope

    virtual ~EpicRtcCallstackInterface() = default;
    
    // Report which types of callstack activity entry/exit functions supported for (for example, just MEMORY if no cpu profiling is wanted)
    virtual EpicRtcCallstackType SupportsEntryExit() const = 0;

    // Upon entry to each scope, method is called with the scope name, whether it's memory/cpuprofile (or both), a pointer to local static void* and stack storage
    virtual void OnEntry(const char* scopeName, EpicRtcCallstackType type, void* volatile* localStatic, uint8_t localStackStorage[LOCALSTACK_BYTES]) = 0;
    
    // Upon exit of each scope, method is called with exact same type and local stack storage that was passed to OnEntry to allow any cleanup
    virtual void OnExit(EpicRtcCallstackType type, uint8_t localStackStorage[LOCALSTACK_BYTES]) = 0;
};

class EpicRtcCallstack
{
    public:
    inline static constexpr int KNOWN_LEAK_UNKNOWN_SIZE = -1;
    struct ScopeInfo
    {
        const char* name;
        const char* flagString;
        const EpicRtcTagInfo* tagInfo;
        int flagInteger;
        EpicRtcScopeFlag flags;
    };

    // Return true if callstack macros are enabled within EpicRtc
    static bool IsEnabled();

    // Assign the callstack interface to relay callstack entry/exit's
    static void SetInterface(EpicRtcCallstackInterface* callstackInterface);

    // Get info about current scope for reporting, tagInfo will be non-null if scope has a tag
    static void GetScopeInfo(ScopeInfo* scopeInfo);

    // Get the current tag for memory tracking, returning inNoTagValue if no tag found
    static const char* GetTag(const char* noTagValue = nullptr);

    // Get the taginfo for a given tag name
    static const EpicRtcTagInfo* GetTagInfo(const char* tagName);

    // Get the head of the taginfo list
    static const EpicRtcTagInfo* GetTagInfoHead();

    // Generate a json string of cpuprofiler info, by default no indentation or newlines
    static EpicRtcStringInterface* GetCpuProfileInfo(int indent = -1);
};
