# Wet road reflections (#183)

## Retail trace

The read-only trace used Capstone 5.0.7 against the supplied `speed.exe`
(SHA-256 `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`)
and XOR-decoded `fx` sources. Each read was bracketed by the 476-file retail
manifest; before and after matched.

- `0x0040C890` selects world effect 2 when the loaded track solid's `+0x0C`
  flags contain `0x40`. That effect is `fx/worldreflect.efx`.
- `0x0040C9D0..0x0040CA54` appends visible material groups from the selected
  track solid. Material texture-info bytes `+81` and `+84` select the queue
  row; the selected effect is the queue column.
- `0x0040CA26..0x0040CA38` has a conditional override when the current
  effect is 1, the material has more than one group, and the texture record's
  `+0x56` byte is nonzero. It assigns the effect-0 descriptor at `0x71ABB8`;
  it does not select effect 3. The trace does not establish a track draw that
  selects `fx/worldheadlightreflect.efx`.
- `0x0040ED50..0x0040F0CF` drains the effect/row queues. It calls
  `0x0040F3D0` per material group; that routine skips unchanged effect and
  texture state, then binds the effect texture parameter through its vtable
  at `0x0040F41D` (offset `+0xD0`).
- `worldreflect.efx` declares `ReflectedTex` and `ReflectedProj`, projects a
  world vertex and samples the reflection texture; its color term is
  multiplied by vertex alpha and diffuse alpha. Its source contains one-pass
  and two-pass techniques, but the static pass count does not prove which
  technique runs. `worldheadlightreflect.efx` also contains projected
  reflection sampling and a `HeadlightTex` sample; the trace above does not
  prove that this effect is selected for track draws.

The static trace establishes effect selection and the reflected-texture
consumer. It does not establish the runtime producer for `ReflectedTex`, the
contents of each reflected render, or per-section light values. The port has
no equivalent reflection target today. The implementation below is therefore
an explicitly visual approximation, not a claim of pixel-identical retail
reflection.

## Port behavior and cost

`--wet-road-reflections on|off` controls the CPU track-path render and the
Switch race command. It defaults to `off`. The shader adds an upward-normal and
road-height mask, a 12 percent maximum base sheen, and two broad colored
streak ribbons from the road UVs. It adds no draw, sampler, texture upload, or
render target. CPU reference rendering applies the same mask and color math.
The source tracks `Global/GLOBALB.BUN`; it does not use `GlobalB.lzc` or
`GLOBALA.BUN`.

The estimate before implementation was 0.1-0.4 ms at 1280x720, assuming about
300,000 shaded road fragments and two sine terms plus a few arithmetic
operations per selected fragment. This is a workload estimate, not a Switch
measurement. The known handheld scene mean is 20.59 ms against a 16.67 ms
target, so the effect cannot be declared within budget until the coordinator
captures the paired console profile.

The `#157` hand-given 1003 path was rendered at samples 0, 3000 and 6000,
1280x720, with the same 6903-step path and cost hashes in both modes:

- path sequence SHA-256: `F64185A1E34258C03126E86EC80DC021716D3F0FE509933F6A8B0BD4D18CB643`
- path cost SHA-256: `0AF10104BAB722F0E186CB0861B76BA6F01ED90144363F6D0D394091350C9D5C`

| Sample | Dry TGA SHA-256 | Wet TGA SHA-256 |
| ---: | --- | --- |
| 0 | `A8270F7AA0DC1671807992BB24947B5FAD5843370032AE3A0183D90224ADD43C` | `9534DC2C76C58232FCF992C59658C9EE0CCF3503980F4B46BFB124818CBC3F5F` |
| 3000 | `FB345706C07DDE9DF20F42501922D238181F0C668D6F9B39B05EABD9E4E92507` | `51FB1A4D035E74C557A289437AEFC4810DA89C4776BDD760105C4B39E6204750` |
| 6000 | `EF5797014996C2A3813A50148F38982BC92A5840B37F009FDD93099D487381EF` | `6ECE035C462F762275AC32EEC82C1C9657CCFE488FFDEC85E33C83D599FB5F53` |

The generated frames were kept outside source control. The supplied retail
screenshots were inspected in place and were not copied into this repository.

## Verification

- Default build: 103/103 CTest tests passed, including all 19 retail tests.
- Release build: 105/105 CTest tests passed, including full-draw and full-slice
  retail checks.
- Switch build: 22/22 CTest tests passed after the profile page was updated to
  label the wet state.
- Retail source manifests contained 476 files and matched before and after
  every retail-backed run.
- The event-1 replay remained at 48,336 steps, total 13:26.90, state SHA-256
  `79776F6DD3FC2727C99D3F6AD69A16E623BDEEB5CFFD2261458A15FC7F5B3BF9`, and
  steps SHA-256
  `E4D4D6E955D59F7FF11C3318BC455D08B7903CA46BBDE8E1FC3F2AC32AEA54E5`.

## Pending console pair

The coordinator must launch the same pinned event-1 replay twice from the PR
head, once with `--wet-road-reflections off` and once with it `on`, then
capture GPU SCENE page 7 and REPLAY page 2 after 60 seconds. Preserve the
replay's 48,336 steps, state hash, steps hash, and 13:26.90 result. Compare the
same frame phase, mode and clocks. The `CONSOLE_REQUEST` comment records the
exact request; this document will be updated with the returned measurement.
