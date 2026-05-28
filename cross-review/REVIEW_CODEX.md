### 1. Option Enumeration


| Option | What It Means | Realistic? |
|---|---|---|
| D-now all | Fix F-001~F-006 in codegen and update `Flit` / `RegisterFile`. | Overbroad for a first-round harness; Stage 2 is explicitly deferred (`docs/superpowers/specs/2026-05-27-c-model-bootstrap-design.md:152`, `:156`). |
| D-defer all | Leave all six workarounds until Stage 2. | Too lax; F-004 and F-006 are already false behavior in `RegisterFile`, not just future gaps (`c_model/src/register_file.cpp:56`, `:66`, `:69`). |
| D-min now | Fix only current-behavior correctness/drift risks: F-004, F-005, F-006; optionally F-001. Defer F-002/F-003. | Best option. It removes live lies from `RegisterFile` while not spending days on payload APIs that Stage 2 may never use. |
| D-flit only | Fix F-001~F-003 but leave registers. | Wrong priority; payload is stubbed future API, but register reset/access bugs affect existing methods now (`c_model/include/register_file.hpp:19`, `:20`). |
| Accept permanent limitations | Document all six as intentional. | Not credible unless APIs are also narrowed; current public methods promise behavior they do not implement (`c_model/include/flit.hpp:22`, `:26`; `c_model/include/register_file.hpp:19`, `:20`). |
| Quarantine workarounds | Keep codegen unchanged, but rename/stub APIs as unsupported and make tests assert unsupported behavior. | Viable if you choose D-defer, but then stop pretending these are sufficient models. |

### 2. Criteria

| Criterion | Why It Matters |
|---|---|
| Invariant 2 fidelity | The design says c_model must not hardcode spec values and violations are drift bugs (`docs/superpowers/specs/2026-05-27-c-model-bootstrap-design.md:12`). |
| Live behavioral correctness | A workaround inside an active API is worse than a missing feature. `RegisterFile::read32/write32/reset` are public (`c_model/include/register_file.hpp:19`, `:20`, `:25`). |
| Test honesty | Tests that pass through stubs create false confidence; `PaddingFieldStaysZero` only calls a method returning unconditional true (`c_model/tests/test_flit.cpp:42`, `c_model/include/flit.hpp:92`). |
| Drift surface size | Manual lists must track spec growth; `known_offsets()` manually lists 31 register offsets (`c_model/src/register_file.cpp:12`-`:43`). |
| Stage 2 coupling | Payload/channel work matters mainly when Layer B uses `Flit`; Layer B is explicitly out of scope/deferred (`docs/superpowers/specs/2026-05-27-c-model-bootstrap-design.md:118`, `:152`). |
| Cost of delayed repair | Deferring behavior bugs means future tests may be written around wrong semantics, especially RW1C/WO and reset. |

### 3. Weighting

| Finding | Residual Hardcode/Deadcode | Severity Here | Evidence |
|---|---|---:|---|
| F-001 | Six-name string dispatch; generated packet has more header fields than the c_model table. | Medium | `flit.hpp` hardcodes `dst_id/src_id/axi_ch/last/rob_idx/rob_req` (`c_model/include/flit.hpp:40`-`:45`); generated packet includes fields like `vc_id`, `route_par`, `multicast`, `flit_ecc` (`spec_validate/generated/ni_packet.json:70`, `:78`, `:118`, `:126`). |
| F-002 | Padding check is dead behavior. | Low-Medium | Finding says unconditional true (`c_model/SUFFICIENCY_FINDINGS.md:11`-`:15`); implementation returns true (`c_model/include/flit.hpp:88`-`:92`); disabled fields exist (`spec_validate/generated/ni_packet.json:83`, `:115`, `:123`, `:131`). |
| F-003 | Payload setters/getters are public no-op/dead API. | Low now, High in Stage 2 | Public API exists (`c_model/include/flit.hpp:22`-`:23`); setter does nothing and getter returns empty (`c_model/include/flit.hpp:95`-`:101`); payload channels are generated (`spec_validate/include/ni_flit_constants.h:72`-`:79`). |
| F-004 | Reset all-zero is already wrong for at least one generated reset. | High | Implementation resets every known offset to zero (`c_model/src/register_file.cpp:53`-`:57`); generated register data has `reset_expr: "0xFFFF"` (`spec_validate/generated/ni_registers.json:138`). |
| F-005 | 31-entry offset list is hand-maintained. | High | Manual set spans `PKT_PROBE_EN_OFFSET` through `EXCLUSIVE_MONITOR_STATUS_OFFSET` (`c_model/src/register_file.cpp:12`-`:43`); finding explicitly calls it hardcoded (`c_model/SUFFICIENCY_FINDINGS.md:27`-`:30`). |
| F-006 | Access-mode enforcement is stubbed false. | High | Generated spec has RW1C and WO registers (`spec_validate/include/ni_regs.h:80`, `:91`); c_model helpers return false (`c_model/src/register_file.cpp:65`-`:69`); write path stores raw value unconditionally (`c_model/src/register_file.cpp:117`). |

### 4. Blind Spots

| Blind Spot | Why It Changes The Decision |
|---|---|
| “Stage 2 deferred” does not make register semantics future-only. | `RegisterFile` already exposes `read32/write32/reset` now (`c_model/include/register_file.hpp:19`-`:25`), and F-004/F-006 affect those methods now. |
| “Uses codegen constants” is not enough for Invariant 2. | F-005 references generated offset constants but still hardcodes the membership list (`c_model/src/register_file.cpp:12`-`:43`), so additions/removals can drift. |
| Tests are validating the workaround, not the intended model. | Reset test is named “zero for now” (`c_model/tests/test_register_file.cpp:14`), while generated reset data already contains nonzero reset (`spec_validate/generated/ni_registers.json:138`). |
| Public no-op APIs are sticky. | `set_payload_channel/get_payload_channel` are already part of `Flit` (`c_model/include/flit.hpp:22`-`:23`), but currently do no useful work (`c_model/include/flit.hpp:95`-`:101`). |

### 5. Recommendation

Do **D-min now**, not D-now all and not D-defer all.

Fix **F-004, F-005, F-006 now**. These are active `RegisterFile` correctness and drift issues: reset is all-zero despite generated nonzero reset data (`c_model/src/register_file.cpp:56`-`:57`, `spec_validate/generated/ni_registers.json:138`), mapped offsets are a 31-entry manual table (`c_model/src/register_file.cpp:12`-`:43`), and RW1C/WO are generated but ignored (`spec_validate/include/ni_regs.h:80`, `:91`; `c_model/src/register_file.cpp:66`, `:69`, `:117`).

Fix **F-001 now only if `Flit::set_header_field/get_header_field` are considered production API**. The current six-name dispatch is a manageable wart, but it already omits generated header fields (`c_model/include/flit.hpp:40`-`:45`; `spec_validate/generated/ni_packet.json:70`, `:118`, `:126`). If `Flit` stays in tests only, defer it with an explicit unsupported-field policy.

Defer **F-002 and F-003** until Layer B starts. Padding validation and payload mapping are real gaps, but today they are mostly inert `Flit` API surface (`c_model/include/flit.hpp:88`-`:101`), and Layer B is explicitly deferred (`docs/superpowers/specs/2026-05-27-c-model-bootstrap-design.md:152`).

### 6. Verdict

**D is conditional on scope: full D is not necessary; D-min is necessary.**

Confidence: **high**.

Top concern: `RegisterFile` currently passes tests while violating generated reset/access semantics.
