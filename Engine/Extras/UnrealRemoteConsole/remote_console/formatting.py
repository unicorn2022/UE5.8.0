"""Log output formatting: syntax highlighting, color-coding, category colors."""

import re
import zlib

_COLOR_ENABLED = True

# UE log messages are color-coded to improve readability. The rules are
# listed below. Each rule references the --test assertions that verify it.
#
# 1. LOG CATEGORIES -- e.g., [LogRHI], [LogEngine], [None]
#    - The server sends the category in the "cat" field; it is rendered as
#      a leading [Category] tag.
#      Tests: "category [LogEngine] colored", "category preserved in plain text"
#    - Additional leading [Tag] patterns in the line text (e.g., [SubSystem])
#      are also detected and colored.
#      Tests: "leading [SubSystem] tag colored", "leading tag text preserved"
#    - Each category gets a deterministic color via CRC32 hash into a curated
#      256-color palette. The color is stable across runs.
#      Test: "category color is deterministic"
#
# 2. VERBOSITY -- Fatal, Error, Warning
#    - Fatal:   bold bright red, with "Fatal: " prefix
#      Test: "fatal verbosity label shown"
#    - Error:   bright red, with "Error: " prefix
#      Tests: "error verbosity label shown", "error text preserved"
#    - Warning: bright yellow, with "Warning: " prefix
#      Test: "warning verbosity label shown"
#    - Log/Display/other: no special styling, no prefix
#      Test: "no verbosity label for Log level"
#    - Verbosity color applies to the entire message text (after categories).
#
# 3. QUOTED STRINGS -- "double" and 'single' quoted
#    - Quote marks in dim yellow, string contents in bright yellow.
#      Tests: "double quote mark dim", "double quote content bright",
#             "single quote mark dim", "single quote content bright"
#    - The brightness difference visually separates delimiters from content.
#      Test: "quoted string '\"1\"' colored"
#    - Numbers inside quoted strings are still highlighted using number
#      coloring rules (bright cyan for value, dim cyan for unit suffix).
#      Tests: "number inside double-quoted string colored",
#             "quote marks still dim around number",
#             "number inside single-quoted string colored",
#             "unit suffix inside quoted string colored"
#    - No text is eaten by highlighting.
#      Test: "no text eaten by string highlight"
#
# 4. KEY-VALUE PAIRS -- key:value and key=value
#    - Key name in blue, separator (: or =) in bright magenta (same as ->).
#      Tests: "key 'rhi.syncinterval' colored", "separator ':' colored",
#             "key 'Width' colored in key=value"
#    - Key must start with a letter/underscore, followed by alphanumerics/dots.
#    - Only matches when separator is followed by a non-space value character.
#      A space between ":" and value is allowed only when the value starts
#      with a digit (e.g., "Textures: 0" matches, "LastSetBy: Constructor" does not).
#      Spaces around "=" are allowed (e.g., 'r.Foo = "40"' matches, as do
#      'r.Foo ="40"' and 'r.Foo= "40"').
#      Tests: "spaced = key colored", "spaced = separator colored",
#             "spaced = text preserved", "key= value key colored",
#             "key =value key colored"
#    - Does NOT match :: or == (C++ scope resolution / comparison).
#      Tests: ":: not treated as key:value", ":: text preserved"
#    - Matches "Key: value" with space after separator.
#      Tests: "'Textures' colored as key with space",
#             "'Buffers' colored as key with space"
#    - Examples: r.Nanite:1, Width=1920, rhi.syncinterval:0, Textures: 0,
#      r.DynamicRes.TestScreenPercentage = "40"
#    - No text is eaten by highlighting.
#      Tests: "no text eaten by key:value", "number '1920' colored in key=value"
#    - Standalone = (not part of key=value) is also highlighted.
#      Test: "standalone = highlighted"
#
# 5. NUMBERS -- standalone numeric literals
#    - Bright cyan. Integers, floats, scientific notation.
#      Tests: "number '1' colored", "standalone number highlighted",
#             "512 number colored", "2.4 number colored", "3.5 number colored",
#             "60 number colored", "99.1 number colored"
#    - NOT highlighted when part of an identifier (e.g., SpotLight77).
#      Tests: "number in identifier not highlighted",
#             "identifier+number text preserved"
#    - Unit suffixes are rendered in a dimmer cyan to distinguish them
#      from the numeric value:
#      - Percent: 50%, 0.1%
#        Test: "% suffix colored"
#      - Size units: KB, KiB, MB, MiB, GB, GiB, K, M, G (case-mixed)
#        Tests: "KB suffix colored differently", "space+MB suffix colored",
#               "space+GiB suffix colored"
#      - Time units: s, ms, us, ns
#        Tests: "ms suffix colored", "ns suffix colored", "us suffix colored",
#               "s suffix colored"
#      - Pixels: px
#        Test: "px suffix colored"
#      - Frequency: Hz, hz
#        Test: "Hz suffix colored"
#      - Rate suffixes: MB/s, Kb/s, etc.
#        Test: "MB/s suffix colored"
#    - Both attached (512KB) and detached (512 KB) forms are matched.
#      In both cases the number is bright cyan and the suffix is dim cyan.
#      Tests: "2.4 with space+MB colored", "1 with space+GiB colored"
#    - No text is eaten by highlighting.
#      Test: "unit text preserved"
#
# 5b. DIMENSIONS -- NUMxNUM, NUMxNUMxNUM
#    - Numbers in bright cyan, 'x' separator in bright magenta (same as ->).
#      Tests: "dimension number colored", "dimension x uses arrow color",
#             "dimension second number colored", "dimension text preserved"
#    - Examples: 1920x1080, 256x256x4, 1067x600
#
# 6. ARROWS, OPERATORS, and SEPARATORS -- ->, =, x (in dimensions)
#    - Bright magenta. Used in CVar change notifications (e.g., 1 -> 0),
#      standalone assignment, and as the 'x' separator in dimensions.
#      Tests: "arrow '->' colored", "arrow -> highlighted",
#             "standalone = highlighted", "dimension x uses arrow color"
#
# 7. BRACKET CHARACTERS -- [ ] ( ) { } < >
#    - Medium gray (subtly brighter than surrounding text).
#      Tests: "opening paren highlighted", "closing paren highlighted",
#             "angle bracket < highlighted", "angle bracket > highlighted"
#    - Adjacent brackets are merged into one span (e.g., [[ or ]]).
#      Tests: "double brackets [[ highlighted", "double brackets ]] highlighted"
#    - Only the bracket characters themselves are colored, not their contents.
#    - No text is eaten by highlighting.
#      Test: "no text eaten by brackets"
#
# All color-coding can be disabled with --no-color.
#
# Important: No text is ever removed or reordered. All highlighting is purely
# additive (ANSI escape codes inserted around tokens). The plain-text content
# is preserved exactly as received from the server.
# ---------------------------------------------------------------------------

_CATEGORY_COLOR_PALETTE = [
    31, 32, 33, 34, 35, 36,       # standard bright colors (skip black/white)
    91, 92, 93, 94, 95, 96,       # high-intensity variants
    130, 136, 142, 148, 154, 160, # 256-color range: warm
    166, 172, 178, 184, 37, 43,   # 256-color range: mid
    49, 50, 51, 75, 81, 87,      # 256-color range: cool
    117, 123, 129, 135, 141, 147, # 256-color range: purple/blue
    171, 177, 183, 189, 209, 215, # 256-color range: pink/salmon
]

_VERBOSITY_STYLES = {
    "Fatal":   "\033[1;91m",    # bold bright red
    "Error":   "\033[91m",      # bright red
    "Warning": "\033[93m",      # bright yellow
}

_COLOR_ENABLED = True          # set to False by --no-color

_RESET = "\033[0m"
_STR_COLOR = "\033[93m"        # bright yellow for quoted string contents
_QUOTE_COLOR = "\033[33m"     # yellow for quote marks -- dimmer than contents
_NUM_COLOR = "\033[96m"        # bright cyan for numbers
_KEY_COLOR = "\033[34m"        # blue for key names in key:value / key=value
_UNIT_COLOR = "\033[36m"       # cyan for unit suffixes (KB, ms, Hz, etc.) -- dimmer than numbers
_BRACKET_COLOR = "\033[38;5;245m"  # medium gray for brackets/parens/angles
_ARROW_COLOR = "\033[95m"          # bright magenta for -> arrows


def _disable_colors():
    """Disable all ANSI color output."""
    global _COLOR_ENABLED, _RESET, _STR_COLOR, _QUOTE_COLOR, _NUM_COLOR, _KEY_COLOR
    global _UNIT_COLOR, _BRACKET_COLOR, _ARROW_COLOR
    _COLOR_ENABLED = False
    _RESET = ""
    _STR_COLOR = ""
    _QUOTE_COLOR = ""
    _NUM_COLOR = ""
    _KEY_COLOR = ""
    _UNIT_COLOR = ""
    _BRACKET_COLOR = ""
    _ARROW_COLOR = ""

# Matches tokens for syntax highlighting in log text.
# Order matters: strings first, then key=value/key:value, then numbers,
# then arrows, then single bracket characters.
_HIGHLIGHT_RE = re.compile(
    r"""(?:"(?:[^"\\]|\\.)*")"""       # "double quoted"
    r"""|(?:'(?:[^'\\]|\\.)*')"""      # 'single quoted'
    r"""|([A-Za-z_][A-Za-z0-9_.]*)"""  # key name (group 1) ...
    r"""( ?= ?|: (?=[-+\d.])|:)"""       # ... separator (group 2): = with optional spaces, ": " before number, or bare :
    r"""(?![:=])"""                     # ... but not :: or == (scope/comparison)
    r"""(?=\S)"""                       # ... followed by a non-space value
    r"""|(?<![a-zA-Z_0-9.])"""         # standalone numbers: not preceded by ident
    r"""[-+]?(?:\d+\.?\d*|\.\d+)"""
    r"""(?:[eE][-+]?\d+)?"""
    r"""( ?"""                          # unit suffix (group 3), optional space + unit:
    r"""(?:"""
    r"""[KMGT]i?[Bb]"""               #   KiB, MB, GB, KB, etc.
    r"""|[kKMGT][Bb]?"""              #   K, M, G, k, KB, etc.
    r"""|[mun]?s"""                    #   s, ms, us, ns
    r"""|px"""                         #   px (pixels)
    r"""|[Hh]z"""                      #   Hz, hz
    r"""|%"""                          #   percent
    r"""|bytes?"""                     #   byte, bytes
    r""")"""
    r"""(?:/s)?"""                     #   optional /s rate suffix (MB/s, Kb/s, etc.)
    r""")?"""
    r"""(?![a-zA-Z_0-9.])"""          # not followed by identifier char
    r"""|(?<![a-zA-Z_0-9.])"""         # dimensions: NUMxNUM or NUMxNUMxNUM
    r"""(\d+(?:\.\d+)?(?:x\d+(?:\.\d+)?)+)"""  # group 4: full dimension string
    r"""(?![a-zA-Z_0-9.])"""
    r"""|->|="""                        # arrow and assignment operators
    r"""|[\[\](){}<>]"""               # bracket characters
)


_BRACKET_CHARS = frozenset('[](){}<>')
_DIM_SPLIT_RE = re.compile(r'(x)')      # for splitting dimension strings like "1920x1080"


# Number pattern for highlighting inside quoted strings.
# Same rules as the main regex but without key-value, arrows, brackets, or strings.
_INNER_NUM_RE = re.compile(
    r"""(?<![a-zA-Z_0-9.])"""
    r"""(?:(\d+(?:\.\d+)?(?:x\d+(?:\.\d+)?)+)"""  # group 1: dimension (NUMxNUM)
    r"""|[-+]?(?:\d+\.?\d*|\.\d+)"""
    r"""(?:[eE][-+]?\d+)?"""
    r"""( ?(?:[KMGT]i?[Bb]|[kKMGT][Bb]?|[mun]?s|px|[Hh]z|%|bytes?)(?:/s)?)?)"""  # group 2: unit
    r"""(?![a-zA-Z_0-9.])"""
)


def _highlight_inner_string(text: str, base_style: str = "") -> str:
    """Highlight numbers and dimensions inside a quoted string."""
    result = []
    last_end = 0
    for m in _INNER_NUM_RE.finditer(text):
        start, end = m.start(), m.end()
        token = m.group()
        if start > last_end:
            result.append(text[last_end:start])
        if m.group(1) is not None:
            # Dimension pattern: split on 'x', color numbers bright, x as arrow
            parts = _DIM_SPLIT_RE.split(token)
            for part in parts:
                if part == 'x':
                    result.append(f"{_ARROW_COLOR}{part}{_RESET}{base_style}")
                else:
                    result.append(f"{_NUM_COLOR}{part}{_RESET}{base_style}")
        else:
            unit = m.group(2)
            if unit:
                num_part = token[:-len(unit)]
                result.append(f"{_NUM_COLOR}{num_part}{_RESET}{base_style}"
                              f"{_UNIT_COLOR}{unit}{_RESET}{base_style}")
            else:
                result.append(f"{_NUM_COLOR}{token}{_RESET}{base_style}")
        last_end = end
    if last_end < len(text):
        result.append(text[last_end:])
    return "".join(result) if result else text


def _highlight_log_text(text: str, base_style: str = "") -> str:
    """Apply syntax highlighting to a log message line.

    Colorizes quoted strings, numeric literals, and bracket characters.
    Consecutive bracket characters (e.g., '[[' or ']]') are merged into
    a single colored span.  base_style is re-applied after each highlighted
    token so that verbosity coloring (e.g., red for errors) is preserved.
    """
    result = []
    last_end = 0
    pending_brackets = []

    def _flush_brackets():
        if pending_brackets:
            result.append(f"{_BRACKET_COLOR}{''.join(pending_brackets)}{_RESET}{base_style}")
            pending_brackets.clear()

    for m in _HIGHLIGHT_RE.finditer(text):
        start, end = m.start(), m.end()
        token = m.group()

        if token in _BRACKET_CHARS:
            # If there's a gap between last match and this bracket, flush
            # any pending brackets and append the gap text first.
            if start > last_end:
                _flush_brackets()
                result.append(text[last_end:start])
            pending_brackets.append(token)
            last_end = end
            continue

        # Non-bracket token -- flush any pending brackets first
        _flush_brackets()

        # Append any unhighlighted text before this match
        if start > last_end:
            result.append(text[last_end:start])

        if token[0] in ('"', "'"):
            # Quote marks dimmer, contents brighter, with numbers highlighted
            quote_char = token[0]
            inner = token[1:-1]
            # Highlight numbers inside the string content
            highlighted_inner = _highlight_inner_string(inner, base_style=_STR_COLOR)
            result.append(f"{_QUOTE_COLOR}{quote_char}{_RESET}{base_style}"
                          f"{_STR_COLOR}{highlighted_inner}{_RESET}{base_style}"
                          f"{_QUOTE_COLOR}{quote_char}{_RESET}{base_style}")
        elif token in ('->', '='):
            result.append(f"{_ARROW_COLOR}{token}{_RESET}{base_style}")
        elif m.group(1) is not None:
            # Key-value pattern: color key and separator separately
            key = m.group(1)
            sep = m.group(2)
            result.append(f"{_KEY_COLOR}{key}{_RESET}{base_style}{_ARROW_COLOR}{sep}{_RESET}{base_style}")
        elif m.group(4) is not None:
            # Dimension pattern (e.g., 1920x1080): numbers bright, 'x' dim
            parts = _DIM_SPLIT_RE.split(token)
            for part in parts:
                if part == 'x':
                    result.append(f"{_ARROW_COLOR}{part}{_RESET}{base_style}")
                else:
                    result.append(f"{_NUM_COLOR}{part}{_RESET}{base_style}")
        else:
            # Number token -- render unit suffix in a different color if present
            unit = m.group(3)
            if unit:
                num_part = token[:-len(unit)]
                result.append(f"{_NUM_COLOR}{num_part}{_RESET}{base_style}"
                              f"{_UNIT_COLOR}{unit}{_RESET}{base_style}")
            else:
                result.append(f"{_NUM_COLOR}{token}{_RESET}{base_style}")

        last_end = end

    _flush_brackets()

    # Append any remaining text after the last match
    if last_end < len(text):
        result.append(text[last_end:])

    return "".join(result) if result else text


_category_color_cache = {}


def _category_color(cat: str) -> str:
    """Return an ANSI escape for a deterministic color based on category name."""
    if not _COLOR_ENABLED:
        return ""
    cached = _category_color_cache.get(cat)
    if cached is not None:
        return cached
    # Use CRC32 instead of hash() which is randomized per process.
    h = zlib.crc32(cat.encode("utf-8")) & 0xFFFFFFFF
    idx = h % len(_CATEGORY_COLOR_PALETTE)
    code = _CATEGORY_COLOR_PALETTE[idx]
    # Always use 256-color foreground format to avoid accidentally setting
    # background colors (codes 40-47) or other non-foreground attributes.
    result = f"\033[38;5;{code}m"
    _category_color_cache[cat] = result
    return result


# Matches leading [Tag] sequences that look like UE log categories.
# Must start with a letter, contain only alphanumeric/underscore/colon chars.
_LEADING_TAG_RE = re.compile(r'\[([A-Za-z][A-Za-z0-9_:]*)\]')

# Verbosity levels that don't get a visible label prefix
_QUIET_VERBOSITIES = frozenset({"Log", "Display", "Verbose", "VeryVerbose", ""})


def _split_leading_tags(text):
    """Extract and colorize leading [Tag] patterns, return (colored_prefix, remainder)."""
    pos = 0
    tag_parts = []
    while pos < len(text):
        m = _LEADING_TAG_RE.match(text, pos)
        if m:
            tag_name = m.group(1)
            tag_parts.append(f"{_category_color(tag_name)}[{tag_name}]{_RESET}")
            pos = m.end()
            # Preserve whitespace between tags
            while pos < len(text) and text[pos] == ' ':
                tag_parts.append(' ')
                pos += 1
        else:
            break
    return "".join(tag_parts), text[pos:]


def _format_log_msg(msg: dict) -> str:
    """Format a log event dict as a colored display string."""
    cat = msg.get("cat", "")
    verbosity = msg.get("v", "")
    line = msg.get("line", "")

    # Fast path: no color processing needed
    if not _COLOR_ENABLED:
        prefix = f"[{cat}] " if cat else ""
        v_label = f"{verbosity}: " if verbosity and verbosity not in _QUIET_VERBOSITIES else ""
        return f"{prefix}{v_label}{line}"

    # Apply verbosity styling to the whole line for errors/warnings/fatals
    v_style = _VERBOSITY_STYLES.get(verbosity, "")
    v_reset = _RESET if v_style else ""

    # Color the server-provided category
    prefix = ""
    if cat:
        prefix = f"{_category_color(cat)}[{cat}]{_RESET} "

    # Show verbosity label for non-default levels so errors/warnings are obvious
    verbosity_prefix = ""
    if verbosity and verbosity not in _QUIET_VERBOSITIES:
        verbosity_prefix = f"{v_style}{verbosity}: {_RESET}"

    tag_prefix, remainder = _split_leading_tags(line)

    # Syntax-highlight only the non-tag remainder
    highlighted = _highlight_log_text(remainder, base_style=v_style)

    full = f"{prefix}{tag_prefix}{verbosity_prefix}{v_style}{highlighted}{v_reset}"
    return full
