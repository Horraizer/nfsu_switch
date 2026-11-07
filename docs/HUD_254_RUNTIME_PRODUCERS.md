# Lap HUD producer provenance and remaining runtime boundary

Research for [issue #254 assignment8244](https://git.accad.pro/murzintsevav/nfsu-switch/issues/254#issuecomment-8244),
on reviewed [PR290](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/290)
at `8d17b4e2b5ffe9ef31645365cd9b0771c36118b0`. This extends
[HUD_254_PRESENTATION.md](HUD_254_PRESENTATION.md). It changes no C++, drawing
gate, gameplay state or acceptance status. The concurrent explicit-input
adapter belongs to scope18; this branch contains research only.

## What the witnesses establish

`tools/hud254_producers.py` authenticates the executable, HUD package, English
font file and world effect by SHA-256 before using them. Original x86 executes
in Unicorn. The report separates these executions from static source evidence.
It records instruction-range hashes, tool hashes, the supplied source commit,
declared stubs and full before/after input manifests, including directories.
There is no original game process, loaded owner profile, captured frame or GPU
execution in this experiment. Initialized emulated memory is not live state.

The existing input identities remain those in `hud254_trace.py`. The added
effect input is `fx/world.efx`, size 2404 bytes, SHA-256
`D1CDBD788D564C4CD408A060D0EDA4E99245F0EB54B18A4FCEE848D2966E6C98`.
This identifies this installation's file; it does not certify the installation
as stock. Its input manifest also includes installed DLL/ASI patches. They are
not loaded, and their effect on an actual process is outside the PE witness.
No extracted effect source, texture, binary, private transcript or reference
image is committed. Reports are local metadata and instruction traces.

## Profile option before permission copying

The static constructor at `0x68AE60` passes the global profile manager to
`0x4A9BC0`. Its calls at `0x4A9BDD..0x4A9C08` construct the first profile and
an array of two more profiles using `0x4AA200` and stride `0xF944`.
One constructed profile begins at `0x7588C4`, the player-zero source read by
the reviewed permission copy. `0x4AA200` passes profile `+0x10` to
`0x4A9DD0..0x4A9E28`. The initializer writes one to option `+6`, hence
`0x7588DA`. That is direct producer provenance for the initialized lap option.

This is not an unconditional event0 default. The options serializer's context
`0x4AAAD0..0x4AAB31` writes a header and name before copying the option block.
The inverse context `0x4AAC70..0x4AACBA` copies the block to profile `+0x10`
when the supplied serialized length is sufficient. The witness executes that
original copy with explicitly constructed bytes, then the existing settings
initializer, player-zero profile copy and view copy. The tests distinguish
initialized, loaded-off, loaded-on and noncanonical option bytes. No options
file from the owner is read or exported.

The new-career confirmation context `0x4B37D0..0x4B388B` calls career reset
and event selection; it is not the profile-options constructor above. The
known career builder still reads current profile options through `0x4B47A0`.
There is no evidence here that entering event0 necessarily reinitializes the
lap option. To resolve the active permission, record the selected profile
option, resulting race settings and view permissions on that actual entry.
The full movie/pause/retry/finish callback sequence remains unobserved.

## Resource binding and serialized hierarchy

The new parser walks bounded FEng/Obj/FOb/ObjD enclosures rather than searching
for a signature inside arbitrary script bytes. It records object identity,
parent ID, resource index, SA fields, script presence, package offset and
object-enclosure hash. A missing/forward parent, duplicate ID, unsupported
template inheritance or truncated field fails the witness.

The actual lap/time leaves and backings have no `PA` field in the pinned
`HUD_SingleRace.fng`. Native loader context `0x50185F..0x501887` resolves `PA`
against an earlier object ID. `0x5015C3..0x5015DC` selects root insertion
`0x500820` or group-child insertion `0x4FBC30`. The probe uses those original
insertion routines to reconstruct the whole package's links from the data.
It does not run the full package loader, animation scripts or screen lifecycle.
The drawing entry `0x4F0390` starts with identity transforms and the actual
callback table at `0x6C1B18`. Native traversal assigns parent index zero to
the lap/time objects. Other package groups exercise the nonzero and nested
parent paths, so this is not an assumed identity-only callback implementation.

Resource names from the real RsRq records pass through original
`0x4F5360..0x4F5487`, which strips the directory/extension, uppercases the
basename, hashes it and initializes the resolved handle. The real object
resource index then passes through `0x5003C0`. Independent tests cross-check
the resulting English font requests with the FNTF metadata. This adds the
name-to-runtime-hash producer to PR290's font loader/lookup evidence.
It does not establish which fonts remain loaded after language changes,
resource replacement, scripts or runtime object mutation. No synthetic font
handle is offered as evidence of the active event0 font registry.

## Traversal changes the equal-depth ordering contract

`0x4F0390` walks package root links. `0x4F04E0` visits group children and
establishes indexed parent transforms; `0x4F0660` collects eligible leaves
with a transformed float depth. `0x4F16D0` performs the stable ascending
sort exercised by the positive-depth package case. The actual callback
`0x4F5090` visits that sorted array backwards, then `0x4F5640` checks leaf
visibility and package eligibility before enqueueing into the bucket queue.

Consequently, equal-depth package ties are reversed before PR290's stable
bucket queue receives them. A port preserving serialized order for such ties
would not implement this producer chain. The lap order and root parent indices
are pinned by `test_lap_order_is_reverse_package_ties_then_depth_buckets`.
General negative-depth sorting is not inferred from the positive-depth case.
For grouped leaves, collection and bucketing use separate float operations;
the rounded collection depth must not be treated as the bucket oracle.

These executions use unanimated serialized SA fields and supplied registry
activation. The package-registry lookup, bounded queue allocator and terminal
object dispatch are the three explicit stubs. Original group traversal, sort,
reverse visitation, visibility checks, bucket insertion and draining execute.
Separate cases supply hidden lap leaves and an inactive package. They test
the gates, not event0's actual lifecycle decisions. Cached dispatch at
`0x4F2CB0` can reuse a previous descriptor; it is captured before geometry,
and the report does not assert that this cache was populated in a live frame.

## Viewport and material path

`0x40ABD0..0x40AC7B` flushes pending work, selects render/depth targets, and
requests a viewport with zero X/Y, width/height from target `+0x14/+0x18`,
and depth interval zero to one. It then records the active target and clears
the current shader context. The native witness captures those requests for
supplied dimensions, with empty-flush and COM stubs. This proves the setter's
contract, not the selected resolution of the original process.

The FEng graphics context `0x409CD0..0x409EC2` selects target `0x71AA30` or
`0x71AA4C` according to its argument, runs that viewport setter, selects effect
context `0x71ABB8`, resets the view/projection matrices, disables depth writes
and depth testing, and reaches the package drawing entry. Target dimension
production at `0x40AA99` reads the configured width/height globals.
The previous NDC conversion therefore composes with a full target viewport
on this path. There is no margin adjustment in this setter. This is not proof
that other runtime transforms, patches or target producers cannot change the
effective safe area, aspect ratio or placement. No screenshot scale is inferred.

Effect initialization `0x40E180` indexes context zero at `0x71ABB8` and the
filename table entry at `0x7014E0`: `fx/world.efx`. The original byte-XOR loop
at `0x40E34D..0x40E35C` decodes the pinned effect in the probe. The report
retains its hash and a contract summary, not the effect source. The file has
a terminal decoded FF byte; compiler treatment of it and compilation success
are not observed. Initialization validates techniques with the device and
records the selected index/handle at context `+0x0C/+0x10`; those live values
are missing.

Both listed source techniques use texture-times-vertex-color modulation by
two for RGB and alpha. The programmable vertex stage applies the WVP matrix
and texture offset; the fixed technique uses the texture-offset matrix. The
source requests wrap addressing and linear min/mag/mip filtering. Its alpha
test/blend parameters are supplied shared values, consistent with PR290's
native material-state producer. Thus halved packed FEng colors are not final
pixel opacity: the effect's factor of two must be included. Actual texels,
selected technique, shared parameter values, device clamping/filtering and
final compositing are not measured by this static contract.

## Remaining evidence and callable boundary

| Recovered input contract | Evidence still needed for production |
| --- | --- |
| Initialized lap option and serialized override/copy path | Active profile identity and bytes after loading/options changes, copied race/view values at event0 entry and each lifecycle transition |
| Default font request hashes and root lap hierarchy | Active package/font registry, selected language, post-animation object fields, parent indices and resource bindings at a named original frame |
| Full-target viewport setter and FEng effect-context selection | Actual target dimensions, matrices, patch state and effective safe-area/scale at that frame |
| Effect RGBA modulation and requested material parameters | Successful device technique selection, effective parameter values and a matching original raster capture |
| Native package collection/sort/reverse/bucket order | Actual package activation, visibility, animation state and cached-descriptor invalidation for each transition |

An adapter may use the proved default contracts when their preconditions are
explicit inputs. It must preserve unknown state for the missing runtime values.
`presentation_ready` remains false. Full event0 acceptance, original footage,
the integrated Switch comparison and owner hardware feedback remain open.
The smallest next evidence artifact is an authenticated original event0 trace
with executable/data/patch identities, selected profile, a named lifecycle
point, the listed globals/object fields and a synchronized unscaled frame.
An emulator initialized from package bytes cannot substitute for that trace.

## Reproduction

Python 3.8+ with Capstone 5.0.7 and Unicorn 2.1.4 is required. From the primary
workspace derive the reference root before entering the assigned worktree:

```powershell
$reference = Join-Path (Split-Path (Get-Location).ProviderPath -Parent) 'reference'
Set-Location build/night20261003-b5-s4-hud-producers254
python -B tools/hud254_producers.py --root 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' --reference $reference --source <full-head> --out out/254-producers-fresh
python -B tools/hud254_producers_test.py --report out/254-producers-fresh --source <full-head>
```

Choose a fresh ASCII output name for every run. Reports cannot overwrite an
earlier report or overlap an input tree. Both complete input manifests must
match, including after a failed probe. The test runner requires the expected
source SHA, checks the tool hashes and unchanged manifests, and has no
report-free pass mode. Expected measurements live in that test, not here.
These are research gates, not C++/Release/retail CTest or hardware acceptance.
