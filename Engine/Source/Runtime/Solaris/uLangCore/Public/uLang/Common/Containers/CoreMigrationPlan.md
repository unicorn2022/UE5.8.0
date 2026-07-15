# uLang → UE Core container migration plan

## Context

`Core` is now reattached to the uLang module graph. With Core visible, every uLang container has a potential Core analog, and the codebase is paying a "two overlapping namespaces" tax — ambient uLang names like `TArray` and `TMap` now collide with Core's and require explicit qualification. The goal is a gradual migration to Core types — one uLang type at a time — **focused on eliminating obvious duplication, not all possible duplication.** Types with semantic mismatches or pending design decisions are pushed to "Out of scope" rather than forced into scope.

This is an analysis + sequencing plan. No code changes in this CL.

---

## Part 1 — Active migration (obvious duplication)

These uLang types have **high-compatibility** Core equivalents with matching names, matching APIs, and no design-level disagreements. The cost is mechanical, and the payoff is immediate: one less duplicate type in the namespace.

### Progress checklist

Tick off as each item lands. Flip `- [ ]` to `- [x]` on submit.

- [x] **Phase 0** — Remove leaky `using namespace uLang;` directives
- [ ] **Phase 1** — Dead code sweep
  - [ ] `TSRefG` (explicit-allocator form)
  - [ ] `TWeakSPtr`
  - [ ] `TDirectedGraph` (+ its unit tests)
  - [ ] `TOPtr` + `CObservedMix` + `ULANG_CHECK_OBSERVER_POINTERS`
- [ ] **Phase 2** — Trivial mechanical renames
  - [ ] `TKeyValuePair` / `TPair` → Core `TPair`
  - [ ] `TQueueG` → `TQueue`
  - [ ] `TFunction` / `TUniqueFunction` / `TFunctionRef` → Core equivalents
- [ ] **Phase 3** — Core containers
  - [ ] `TMap` / `TMapG`
  - [ ] `TSet` / `TSetG`
  - [ ] `TArray` / `TArrayG` / `TArrayA` (expect per-module CLs)
- [ ] **Phase 4** — `TOptional` → Core `TOptional` (audit `EResult` users first)
- [ ] **Phase 5** — `TUPtr` → `TUniquePtr`
- [ ] **Phase 6** — `TSPtr` / `TSRef` / `CSharedMix` → `TSharedPtr` / `TSharedRef` / `TSharedFromThis`
  - [ ] Main pointer/ref migration (`MakeShared`, `AsShared`)
  - [ ] `Vst::Node` shared-from-this port
  - [ ] `SemanticScope.cpp:843` invariant rewrite
  - [ ] Delete test-only `GetRefCount()` uses in `TestuLangCore.cpp`
- [ ] **Phase 7** — CUTF8 string family → FUtf8 string family
  - [ ] `CUTF8StringView` → `FUtf8StringView`
  - [ ] `CUTF8String` → `FUtf8String`
  - [ ] `CUTF8StringBuilder` → `TUtf8StringBuilder`
- [ ] **Phase 8** — Allocator cleanup (delete `CHeapRawAllocator`, `TDefaultElementAllocator`; `TInlineElementAllocator` → `TInlineAllocator` rides with Phase 3)

### Phase 0 — Remove leaky `using namespace uLang;` directives (prerequisite)

Before any type-level migration, remove `using namespace uLang;` directives from file scope and from anonymous namespaces in uLang `.cpp` files. At those scopes, the directive makes every `uLang::` name findable unqualified at global scope for the rest of the translation unit — which, in a unity build, includes every `.cpp` concatenated after it. Once a Core header with an identically-named type is pulled in later in the TU, unqualified name lookup sees both candidates and fails with ambiguity or shadow errors. This trips clang (which does stricter template name lookup and enables `-Wshadow` by default under these configs); MSVC is more permissive and often lets it through silently.

**Preferred fix: qualify each usage explicitly with `uLang::`.** Delete the `using namespace uLang;` directive and prepend `uLang::` to every unqualified use that the compiler flags afterward. Do **not** take shortcuts like wrapping the file in a new `namespace uLang { ... }` block or demoting the directive into a narrower scope — those hide the dependency rather than make it explicit. The whole point of this phase is that every site that names a uLang type does so unambiguously, so the reader (and compiler) never has to guess which namespace it means.

No type changes — pure scope hygiene that unblocks everything else.

### Phase 1 — Dead code sweep (one CL)

Delete uLang types with no production uses. Confirmed via grep across `VerseCompiler/`, `Solaris/`, `NotForLicensees/`, `Sandbox/`:

| Type | Use sites | Action |
|---|---|---|
| `TSRefG` (explicit-allocator form) | 0 | Delete declaration; bare `TSRef` stays. |
| `TWeakSPtr` | 0 | Delete declaration. |
| `TDirectedGraph` | 0 production (only its own unit tests in `TestuLangCore.cpp`) | Delete the header and its tests together. |
| `TOPtr` + `CObservedMix` (and `ULANG_CHECK_OBSERVER_POINTERS`) | 0 production (only type-definition headers, `uLangCore.natvis` debugger visualizer, and one unit test) | Delete the headers (`ObserverPointer.h` and the `CObservedMix` base), all `ULANG_CHECK_OBSERVER_POINTERS`-guarded code paths, the `.natvis` entries, and the test. Whole observer machinery goes. |

Near-zero risk. One tidy CL.

### Phase 2 — Trivial mechanical renames (1–2 CLs)

| uLang type | Core type | Notes |
|---|---|---|
| `TKeyValuePair` / uLang `TPair` | `TPair<K,V>` | Field rename `_Key`/`_Value` → `Key`/`Value`. 2,500+ sites, fully mechanical — scripted sed + spot-check. |
| `TQueueG<T, EMode>` | `TQueue<T, EQueueMode>` | 7 uses. Both support SPSC/MPMC. |
| `TFunction<Sig>` | `TFunction<Sig>` | Near drop-in. |
| `TUniqueFunction<Sig>` | `TUniqueFunction<Sig>` | Near drop-in. |
| `TFunctionRef<Sig>` | `TFunctionRef<Sig>` | Near drop-in. |

### Phase 3 — Core containers (one type per CL)

Order: **`TMap` → `TSet` → `TArray`**. Do the smaller-volume ones first to shake out allocator/iteration issues before tackling the biggest.

| uLang type | Core type | Notes |
|---|---|---|
| `TMapG` / `TMap` | `TMap<K,V>` | Core's TMap is TSparseArray-backed; iteration order differs from uLang's THashTable. Iteration-order audit (see "Audit findings" below) found VerseCompiler only directly iterates a TMap in 4 sites — all either map-to-map copy, a sorted-output pipeline already protected by explicit `StableSort`, or a validator. Low risk. One spot-check: `_AllEffectClasses`'s external consumer `FindAllAttributeIdentifiersHack` in `SemanticAnalyzer.cpp:7356` — verify order-insensitive during the CL. Direct `TMapG` uses: 3; many more bare `TMap`. |
| `TSetG` / `TSet` | `TSet<T>` | Audit found **zero** direct TSet iterations in VerseCompiler production code — members like `_Scopes` and `_TypeVariableSubstitutions` that look set-like are actually `TArray`. Migration is mechanical. Direct `TSetG` uses: 3. |
| `TArrayG` / `TArray` / `TArrayA` | `TArray<T>` / `TArray<T, Allocator>` | Thousands of bare uses. Suggested execution: (a) replace explicit `uLang::TArray<...>` qualifications with `TArray<...>` where both resolve identically, (b) retire the `uLang::TArray` alias and fix the remaining ambiguities one module at a time. `TInlineElementAllocator<N>` → `TInlineAllocator<N>`. |

`TArray` is the largest single migration in this plan — expect per-module CLs rather than one big commit.

### Phase 4 — TOptional (one CL per module)

`TOptional<T>` → `TOptional<T>`.

4,200+ uses but mostly simple surface (`IsSet`, `GetValue`, `Emplace`, `Reset`) that matches Core exactly.

**Caveat**: uLang's `TOptional` also carries an `EResult` status (`OK`, `Unspecified`, etc.). Before starting, grep for sites that touch the error state — those should migrate to `TValueOrError<T, EError>` instead, or stay on uLang `TOptional` and move in a later CL. The bulk of the 4,200 uses don't touch `EResult` and can convert cleanly.

### Phase 5 — TUPtr → TUniquePtr

`TUPtr<T>` is a heap-backed nullable alias for `TUPtrG<T, true, CHeapRawAllocator>`. Migrates cleanly to `TUniquePtr<T>` — no intrusive mix-in required. Factory `TUPtr<T>::New(...)` → `MakeUnique<T>(...)`.

In scope for this phase:
- `TUPtr` (bare alias, the common case).
- `TUPtrG<T, AllowNull, CHeapRawAllocator>` explicit uses (42 sites).
- `TUPtrArrayG` (33) which collapses to `TArray<TUniquePtr<T>>` once the element type migrates.

**Not in scope:** `TUPtrA` is aliased to `TUPtrG<T, true, CInstancedRawAllocator, CAllocatorInstance*>` — its migration is gated on the `CInstancedRawAllocator` decision (Out of scope). If `TUPtrA` uses exist, leave them on `TUPtrG` temporarily and resolve with the allocator decision.

### Phase 6 — TSPtr / TSRef / CSharedMix → TSharedPtr / TSharedRef / TSharedFromThis

Audit confirmed this is mechanical, not a design decision. See "Audit findings" below for the full evidence. Summary:

- `TSPtr<T>` (553 uses) → `TSharedPtr<T>`
- `TSRef<T>` (1,500 uses) → `TSharedRef<T>`
- `CSharedMix` base class → `TSharedFromThis<T>`
- Factory `TSPtr::New(...)` / `TSRef::New(...)` → `MakeShared<T>(...)`
- `SharedThis()` → `AsShared()` (2 production sites, both in `SemanticAnalyzer.cpp` — clean lambda captures, not constructor calls)
- Pointer-of-pointer variants (`TSPtrArrayG` 39 uses, `TSPtrSetG` 33 uses, `TSRefArray`) collapse to plain `TArray<TSharedPtr<T>>` / `TSet<TSharedPtr<T>>`.

Specific small sub-tasks:

- **`Vst::Node` shared-from-this port.** `VstNode.h` has its own `MakeSharedFromThis` + `GetSharedSelf` helpers that internally read `GetRefCount()`. Port to `TSharedFromThis<Vst::Node>`; `GetSharedSelf()` returns `AsShared()`. Cleans up the `TNodeRef`/`TNodePtrG` typedef family too.
- **`SemanticScope.cpp:843` invariant rewrite.** One production `ULANG_ASSERTF(Definition->GetRefCount() == 1 + ExtraReference(...))` check. Core hides the count, so either:
  - Replace with `Definition.IsUnique()` (works if `ExtraReference == 0`)
  - Track the extra-reference count explicitly in the class and assert on that
  - Drop the assertion if it's not load-bearing

  Decide in-CL when the surrounding code is in front of us.
- **Delete test-only `GetRefCount()` uses.** Six assertions in `TestuLangCore.cpp` test uLang's ref-counting machinery. Core's is already tested; delete the relevant test cases.

Safety notes (all audit-confirmed, re-list here for reviewers):
1. Raw-pointer-to-TSPtr conversion is impossible — the `TSPtrG(Object*, ...)` constructor is `protected` with only `CSharedMix` as a friend. No caller can accidentally get the "fresh control block / double free" case.
2. No `SharedThis()` call happens during construction.
3. No public `GetRefCount()` reader outside the one invariant above.

### Phase 7 — CUTF8 string family → FUtf8 string family

**Prerequisite verified**: `FUtf8String` (`Containers/Utf8String.h`), `FUtf8StringView = TStringView<UTF8CHAR>` (`Containers/StringFwd.h`), and `TUtf8StringBuilder<N>` + `FUtf8StringBuilderBase = TStringBuilderBase<UTF8CHAR>` (`Containers/StringFwd.h`) all exist in Release-41.00 Core. UTF-8–native, no `FString` UTF-16 detour required.

| uLang type | Core type | Notes |
|---|---|---|
| `CUTF8String` / `TUTF8String<Alloc>` / `CUTF8StringA` | `FUtf8String` | ~1,000 uses. API map: `AsCString()` → equivalent UTF-8 C-string accessor; `ByteLen()` → `Len()` (Core's Utf8String length is byte count). Audit allocator-instanced `CUTF8StringA` uses separately — if any, they ride with the `CInstancedRawAllocator` decision (Out of scope). |
| `CUTF8StringView` | `FUtf8StringView` | 393 uses. Both non-owning `UTF8CHAR` spans. |
| `CUTF8StringBuilder` / `TUTF8StringBuilder<Alloc>` | `TUtf8StringBuilder<N>` / `FUtf8StringBuilderBase` | 92 uses. Core's builder uses `operator<<` / `Appendf` where uLang's uses `Append` / `AppendFormat` — mostly mechanical. |

Order within this phase: **`CUTF8StringView` → `CUTF8String` → `CUTF8StringBuilder`** (view is simpler and acts as a migration check before touching the owning types). Suggest one CL per module for the owning-string migration; view and builder can go broader.

### Phase 8 — Allocator cleanup (bounded scope)

After Phases 3–7 complete:

- Delete `CHeapRawAllocator` and `TDefaultElementAllocator<...>` if no remaining users.
- `TInlineElementAllocator<N>` → `TInlineAllocator<N>` happens per call site during Phase 3, not as a separate phase.

`CInstancedRawAllocator` / `CAllocatorInstance` are **out of scope** — see Part 2.

### Suggested sequencing

```
Phase 0 (using-directive cleanup)
    ↓
Phase 1 (dead code)
    ↓
Phase 2 (trivial renames)
    ↓
Phase 3 (Map → Set → Array)  →  Phase 8
         ↓                          ↑
    Phase 4 (TOptional) ────────────┤
         ↓                          │
    Phase 5 (TUPtr) ────────────────┤
         ↓                          │
    Phase 6 (TSPtr/TSRef) ──────────┤
         ↓                          │
    Phase 7 (CUTF8 → FUtf8) ────────┘
```

Phases 4–7 are independent of each other (and of Phase 3 once containers resolve). Parallelize across contributors.

---

## Part 2 — Out of scope

These types either have no clear mapping to Core or have a mapping that would require a separate design decision. They're out of scope for the "obvious duplication" cleanup. Some may be revisited later; some (like `CSymbol`) are permanent uLang primitives.

### TRangeView → TArrayView / iterator helpers

25 uses. No single Core primitive matches all use cases:
- Non-owning contiguous span → `TArrayView<T>` (high compat).
- General iterator-range pair → no direct Core equivalent; handled case-by-case via raw iterators.

Small enough that it can be converted site-by-site later, but the case-by-case nature means it isn't "obvious duplication." Defer.

### THashTable → (no public Core equivalent)

Core's hash table is an implementation detail behind `TSet` / `TMap`; there's no public `THashTable`-equivalent primitive.

Any direct `THashTable` users that survive after Phase 3 need to be folded into `TSet` / `TMap`, or a thin local wrapper needs to be introduced. Low priority — revisit after Phase 3.

### CInstancedRawAllocator / CAllocatorInstance

uLang's instanced-allocator stack (arena-style, per-instance allocators) is functionally similar to Core's `FMemStack` / `TMemStackAllocator`, but the plumbing is different — instanced-allocator templates vs stack-scoped allocation.

Worth a short design doc of its own once we see how many surviving arena-style uses remain after Phases 2–6. Options: map onto `FMemStack`; keep the uLang stack as-is; or bridge with an adapter. Not "obvious duplication."

### CSymbol (permanent)

417 uses. Both `CSymbol` and `FName` are interned strings, but **`CSymbol` is case-sensitive; `FName` is case-insensitive.** Verse identifiers are case-sensitive at the language level, so `CSymbol`'s case-sensitivity is load-bearing — it is not a migration artifact we can relax. Changing that rule would silently alter program semantics across the compiler's entire symbol table. `CSymbol` and `FName` are not duplicates — they have fundamentally different identity semantics. `CSymbol` stays as a compiler-internal primitive. No migration is planned.

---

## Audit findings — TMap/TSet iteration order

Before assigning `TMap`/`TSet` to Phase 3, audited VerseCompiler for iteration-order dependencies that would break when migrating from uLang's THashTable-backed maps/sets to Core's TSparseArray-backed ones.

| Observation | Details |
|---|---|
| Direct TMap iteration in VerseCompiler | 4 sites, all in `SemanticProgram.cpp`. Three in `InitializeEffectsTables` (`:1878` map-copy, `:1886` builds `_AllEffectClasses`, `:1891` builds `_OrderedEffectDecompositionData` which is then explicitly `StableSort`ed with the comment "alphabetical sorting is important because this code is indirectly used in mangled symbol generation"). One in `ValidateEffectDescriptorTable:1928` — read-only validation. |
| Direct TSet iteration in VerseCompiler | **Zero.** Members that looked set-like (`_Scopes`, `_TypeVariableSubstitutions`) are actually `TArray` — iteration is deterministic by construction. |
| Defensive sorting idiom | Pervasive. Explicit `Sort` / `StableSort` / `Algo::Sort` calls with determinism-tagged comments in `DigestGenerator.cpp` (:91, for digest output), `SemanticProgram.cpp` (:1902, :2096), `SourceProject.cpp` (:262). Codebase already treats uLang's maps/sets as lookup-only. |
| Spot-check for migration CL | `_AllEffectClasses` (a `TArray<const CClass*>` built by iterating `_EffectDescriptorTable`) has one external consumer: `FindAllAttributeIdentifiersHack` at `SemanticAnalyzer.cpp:7356`. Name suggests matching/lookup, but verify order-insensitivity during the CL. |

Conclusion: Phase 3 is low-risk. No design-level blocker; migration can proceed type-by-type with the spot-check above.

---

## Audit findings — smart pointer migration

Before assigning `TSPtr`/`TSRef`/`CSharedMix` to a phase, audited for the patterns that are safe under uLang's intrusive model but UB under Core's non-intrusive model.

| Pattern | Status | Evidence |
|---|---|---|
| Raw-pointer-to-TSPtr conversion (`TSPtr<T>(rawPtr)`) | **Impossible** | `TSPtrG(Object*, Allocator)` constructor is `protected`, `SharedPointer.h:200`. Only `CSharedMix` is a `friend`. No external caller can invoke it. |
| `SharedThis()` / `AsShared()` from a constructor | **No occurrences** | Only 2 uLang `SharedThis()` production sites (`SemanticAnalyzer.cpp:18557, 18819`), both lambda captures long after construction. Other 9 `SharedThis` hits are Slate's (SWidget) — Core code, not uLang. |
| Public `GetRefCount()` production readers | **2 sites** | `VstNode.h:109` (internal to `MakeSharedFromThis`, disappears during port); `SemanticScope.cpp:843` (one real invariant — port to `IsUnique()` or small refactor). Also 6 test-only uses in `TestuLangCore.cpp`, deleted with migration. |
| `TOPtr` / `CObservedMix` in production | **0 production uses** | Only type-definition header, debugger visualizer, and one unit test. Moved to Phase 1 dead-code. |
| Weak-ref extends object lifetime | **Not applicable** | `TOPtr` is dead; no migration target for weak references needed. |

Conclusion: the smart-pointer migration is mechanical. No design decision required. Moved from "Out of scope" to Phase 6 (active migration).

---

## Usage data summary (cost estimation)

From the audit across `VerseCompiler/`, `Solaris/`, `SolarisTestbed/`:

| Volume | Types |
|---|---|
| **>1000 sites** | `TOptional` (4,200+), `TPair` (2,500+), `TSRef` (1,500+), `CUTF8String` (1,000+) |
| **100–1000 sites** | `TSPtr` (553), `CSymbol` (417), `CUTF8StringView` (393) |
| **10–100 sites** | `TArrayG`-explicit (96), `CUTF8StringBuilder` (92), `TSPtrG` (61), `TNodePtrG` (54), `TUPtrG` (42), `TSPtrArrayG` (39), `TUPtrArrayG`/`TSPtrSetG` (33 each), `TRangeView` (25) |
| **<10 sites** | `TQueueG` (7), `TMapG` / `TSetG` (3 each) |
| **0 sites** | `TSRefG`, `TWeakSPtr`, `TDirectedGraph` (production) |

Notes:
- Bare `TArray<...>` inside `namespace uLang` resolves to the uLang alias — the `TArrayG` count (96) is explicit-allocator uses only; the true bare-`TArray` count is much higher and drives Phase 3's cost.
- Hotspot files: `SemanticAnalyzer.cpp`, `Expression.h`, `IRGenerator.cpp`, `SemanticProgram.h`. Expect many CLs here.

---

## Summary at a glance

- **Active migration (eliminate now):** remove leaky `using namespace uLang;` directives (Phase 0); dead types incl. `TDirectedGraph`, `TOPtr` / `CObservedMix`, `TSRefG`, `TWeakSPtr` (Phase 1); `TPair` / `TQueue` / `TFunction` family (Phase 2); `TMap` / `TSet` / `TArray` (Phase 3); `TOptional` minus its `EResult` users (Phase 4); `TUPtr` (Phase 5); `TSPtr` / `TSRef` / `CSharedMix` (Phase 6); `CUTF8String` / `CUTF8StringView` / `CUTF8StringBuilder` → `FUtf8` family (Phase 7); allocator cleanup (Phase 8).
- **Out of scope:** `TRangeView` (case-by-case); `THashTable` (no public Core analog); `CInstancedRawAllocator` (design divergence); `CSymbol` (case-sensitivity is load-bearing — permanent).
