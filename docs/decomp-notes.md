# Decompilation matcher (tools/decomp)

`tools/decomp` matches a Wii U executable's functions to a GameCube decompilation. It was built and measured
for Wind Waker HD (`cking.rpx` against zeldaret/tww) before the fork, and everything below (counts,
precision, addresses, structure) is Wind Waker HD's. It has not been run on Twilight Princess HD
(`Zelda.rpx` against zeldaret/tp, where the brief takes TP HD's function names from). Wind Waker HD's
findings from the matched names (camera, models, interpolation and true 60, timers, layouts, its gameplay
mods and aspect hooks) were removed from this file on 2026-10-09; git history has them.

Function names for WWHD (`cking.rpx`) came from
`python3 tools/decomp/match.py game/code/cking.rpx tww build/names.tsv` (about 1 minute). Needs
`tww/` (zeldaret/tww) built from your own GameCube disc image (`tww/build/GZLE01`). Outputs (all
in git-ignored `build/`):

- `names.tsv`: address, name, source file, evidence, score (match probability for `graph`/`tu`).
- `wwhd_to_gc.tsv`: WWHD address → GameCube mangled symbol, source file, module, GameCube
  address, evidence, both sizes. Stage 2 (checking each function against the source) builds on it.
- `coverage.tsv`: per translation unit, GameCube functions vs. matched.
- `regions.tsv`: long unmatched stretches (HD-only code).

Helper tools: `layout.py` (structure offsets), `timers.py` (frame timers),
`datamap.py` (WWHD addresses of GameCube globals), `heldout.py` (precision test), `train.py`
(refits the pair-scoring model).

### Evidence and measured precision

| Evidence | Meaning | Count | Held-out precision |
|---|---|---|---|
| `manual` | hand-verified (`tools/decomp/manual_names.tsv`) | 11 | — |
| `assert` | assert text (file + condition) | 191 | near-certain |
| `profile` | actor profile method tables (process-name IDs) | 1,477 | near-certain |
| `strings` | rare string/float literals + file neighbourhood | 353 | high (used as test truth) |
| `vtable` | virtual tables and other function-pointer tables (method tables, PTMF tables) aligned slot by slot; also tables identified through the constructors that store them | 623 | 100% (1,689/1,689) |
| `graph` | call-graph neighbourhood + content model, mutual best, p ≥ 0.6 | 3,732 | 97.1% (271/279); 94.3% strict |
| `tu` | unmatched functions between matched functions of the same file, or between two files (alphabetical link order), same model, p ≥ 0.6 | 6,024 | 99.7% (998/1,001); 97.5% strict |
| `tu-margin` | same windows, decided relative to the file's other unmatched functions (score ≥ −1.5, ≥ 2.0 above the runner-up), after the stages above converge | 755 | 96.8% (30/31) held-out; 98.8% (80/81) offline; **medium confidence** |
| `dup`, `dup-graph`, `dup-file` | WWHD's per-file copies of inline functions: named only when that file's GameCube object has a copy of the same function | 40 | not measured (too few) |
| `callgraph-legacy` | the first call-graph matcher, run last, only where nothing else matched | 785 | ~85–90% (older test); **lower confidence** |

The content model scores a pair with 25 features. It is a conditional logit (`train.py`), fitted
on 8,900 trusted pairs against decoys from the same file, from call-graph neighbours and random.
Since round 2 it includes:
- register-agnostic instruction shape (`shape.py`): opcode histogram cosine, opcode-bigram overlap,
  branch/loop/compare/float-op counts, with prologue/epilogue bookkeeping removed;
- per-class field maps (`field_map`): GameCube field offsets accessed through `this` (or the first
  pointer argument) are translated into WWHD offsets with maps learned from matched pairs (as in
  `layout.py`; actor classes fall back to the fopAc_ac_c map) and compared with the WWHD accesses.

Cross-validated, it names 1,527 of 2,500 test pairs at p ≥ 0.7 with 97.6% precision.

How the held-out test works (`heldout.py`): trusted names (assert/strings/profile + tables) are
hidden at random, the matcher runs from the rest, and each hidden function it names again is
checked. Round-2 figures are summed over five runs: three hide 50% of the `assert`/`strings` names
(functions without table evidence, the harder case), two hide 50% of everything. "Strict" counts
naming a wrapper after its own callee as wrong. In WWHD, an actor's `daXxx_Create` often has
`_create` inlined, so its body is the callee's. The "offline" `tu-margin` figure re-decides 2,714
trusted pairs with each one hidden in turn and the rest known.

Known wrong names are listed with `!` in `manual_names.tsv`. For example, `025200D4` is a tiny
static-instance accessor, not `JAIZelBasic::seStart` (and not `__ptmf_scall`). The compiler runtime
(`PowerPC_EABI_Support/Runtime`) is never used as a match target.

### Structural facts used by the matcher

- WWHD links translation units in **alphabetical order per directory** (`SSystem/c_*`,
  `JAZelAudio`, `d/` with the actors in `d/actor` sorted into it, `f_op`, `f_pc`, `m_Do`, ...,
  JSystem, `dolphin/mtx`). Each unit is contiguous. Function order inside a unit is often
  preserved (c_lib) but not always (d_camera).
- Green Hills vtables: 8 bytes per slot (`delta:index` word, function pointer). A zero header slot
  starts each table. Actor classes gained a virtual destructor in slot 0, so WWHD actor tables
  have one more slot than the GameCube ones. `028F036C` (`li r3,13; b ...`) is the
  pure-virtual stub.
- Identical code folding is not done: a GHS build keeps a copy of each out-of-line inline
  function per translation unit (≈12,300 functions in ≈1,000 identical-body groups; 1,504 copies
  of one 20-byte trivial deleting destructor).
- Function discovery in `tools/recomp` merges functions that are only reached by tail calls
  (`b`) into their predecessor. The matcher splits 1,932 of them (`binmodel.refine_functions`).
  The recompiler's own list is unchanged.
