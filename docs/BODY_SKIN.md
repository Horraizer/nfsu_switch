# Body-skin pixel compositor

Clean-room notes on how Need for Speed: Underground (PC) composes a car's
body-skin texture at runtime. Everything here was read out of the retail
executable and out of a user-provided retail installation. No game code and
no game data are stored in this repository.

Reference executable:

- File: `speed.exe`
- Size: 3178496 bytes
- SHA-256: `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`
- PE32, image base `0x00400000`. `.text` is at VA `0x00401000` with raw
  offset `0x1000`, so inside `.text` a file offset equals the RVA. That does
  not hold for `.rdata` (VA `0x00697000`, raw `0x297000`) or `.data`
  (VA `0x006D4000`, raw `0x2D4000`), where the float constants below live.

All addresses below are virtual addresses of that image. Issue #20 already
established which texture the compositor writes to, which inputs it gathers,
the layer order and the paired-mask lookup; see `docs/CAR_SCENE.md`,
"Body-skin composition inputs". This note covers only the per-pixel work.

Terminology: a packed color is four bytes. They are called **lanes** here,
lane 0 being the low byte of the little-endian u32 and lane 3 the high byte.
Lane numbers are used throughout because the two composite paths store the
same color with two different lane orders. The channels themselves are
named, once, in "Channel identity" below; no pixel format is named anywhere,
because no instruction names one.

## Dispatch and the two paths

`0x005A9CE0` gathers the inputs and ends with:

| Address | Instruction | Meaning |
| --- | --- | --- |
| 0x005AA250 | `call 0x005A8400` | request cache lookup; a non-zero result skips compositing |
| 0x005AA25D | `call 0x005A8490` | request cache store |
| 0x005AA262 | `cmp byte ptr [ebx+0x4a], 8` | target texture object type byte |
| 0x005AA26D | `call 0x005A8E90` | taken when the byte is 8 |
| 0x005AA279 | `call 0x005A8A50` | taken otherwise |

`0x005A8A50` returns 0 at once unless the same byte is `0x20`
(`cmp byte ptr [edi+0x4a], 0x20` at 0x005A8A7E), and `0x005A8E90` returns 0
at once unless it is 8 (0x005A8EB9). The two routines are therefore called
the **type-32** and the **type-8** path here, after that byte. Nothing in
either routine names a pixel format.

Both take exactly one pushed pointer to a parameter block and return 1 on
success, 0 when they refused.

## Parameter block

Built in the caller's frame at `[esp+0x54]` and copied from `[esp+0x210]`
(0x005AA1FA..0x005AA24B). Offsets are relative to the block.

| Offset | Size | Field | Evidence |
| ---: | ---: | --- | --- |
| +0x000 | 4 | target texture object | 0x005A8A64, 0x005A8EA7 |
| +0x004 | 4 | base color | 0x005A8A59, 0x005A8E9D |
| +0x008 | 4 | special color 0 | 0x005A8B80, 0x005A90E3 |
| +0x00C | 4 | special color 1 | 0x005A8BA3, 0x005A90E9 |
| +0x010 | 4 | special color 2 | 0x005A8BBE, 0x005A90EF |
| +0x014 | 4 | special color 3 | 0x005A8BDB, 0x005A90F5 |
| +0x018 | 8 x 0x34 | layer records | 0x005AA249 copies 0x1A0 bytes |
| +0x1B8 | 4 | filled layer count, at most 8 | 0x005A8A5C, 0x005A8EA0 |

All five colors are packed the same way, from the `RED`, `GREEN`, `BLUE`
and `GLOSS` attributes of one installed part (see "Channel identity"). The
base color comes from the part in slot 30, `BASE_PAINT`
(0x005A9D46..0x005A9DAD). The four special colors come from slots 0x25,
0x26, 0x27 and 0x28, that is 37..40, in that order
(0x005A9DB1..0x005A9DC9, read at 0x005A9DE0):

| Special | Slot | Name | Marker it replaces |
| ---: | ---: | --- | --- |
| 0 | 37 | `PAINT_ENGINE` | red |
| 1 | 38 | `PAINT_SPOILER` | green |
| 2 | 39 | `PAINT_BRAKE` | blue |
| 3 | 40 | `PAINT_EXHAUST` | magenta |

An empty slot falls back to the base color, which 0x005A9EA2 keeps in `edi`
across the loop (0x005A9EBF). `PAINT_RIM`, slot 41, belongs to the separate
wheel compositor at 0x005AA800.

0x005A9ED3..0x005A9EF4 zeroes all 0x1A0 bytes and starts two counters at
zero. The outer loop then runs a **source index** `ebx` from 0 to 7 and
dispatches on it (0x005A9F00):

| Source index | Source | Helper |
| ---: | --- | --- |
| 0 | hood or spoiler vinyl | 0x005A9930 |
| 1..4 | `VINYL_LAYER0`..`VINYL_LAYER3` | 0x005A9B50 |
| 5 | doorline | 0x005A96B0 |
| 6, 7 | nothing | none |

The **record cursor** and the count advance only when that source produced
a usable record: the five failure paths at 0x005A9FE0, 0x005A9FFF,
0x005AA039, 0x005AA070 and 0x005AA0A7 all jump to 0x005AA1E0, past the
`inc ecx` and `add ebp, 0x34` at 0x005AA1D4. The array is therefore
compacted: records 0..count-1 are the sources that resolved, in the order
above, and the rest stay zeroed. Record 0 is composited first
(0x005A8C3A, 0x005A8E12).

A source is dropped when its name hash is zero (0x005A9FE0), when the
source texture is not found (0x005A9FFF), when the `_MASK` texture is not
found (0x005AA039), or when either lock fails (0x005AA070, 0x005AA0A7).
The four vinyl layers are also de-duplicated against the earlier ones
(0x005A9F25..0x005A9F3A). A missing or zero `NUMCOLOURS` on a vinyl layer
is different: it aborts the whole composite, which returns 0
(0x005A9F93..0x005A9FA4).

### Layer record, 0x34 bytes

| Offset | Field | Evidence |
| ---: | --- | --- |
| +0x00 | source texture name hash; zero skips the record | 0x005A9F53, 0x005A9FEB; tested 0x005A8C41, 0x005A9140 |
| +0x04 | source texture object, unlocked afterwards | 0x005AA290..0x005AA2A3 |
| +0x08 | mask texture object, unlocked afterwards | 0x005AA2A6..0x005AA2BA |
| +0x0C | source pixels | 0x005A8CC6, 0x005A914F, 0x005A9292 |
| +0x10 | source palette, only when the target type is not 0x20 | 0x005AA054, 0x005AA066; used 0x005A9159 |
| +0x14 | mask pixels | 0x005A8CCD, 0x005A9164 |
| +0x18 | mask palette, same condition | 0x005AA08B, 0x005AA09D; used 0x005A915C |
| +0x1C | `NUMCOLOURS`, how many palette entries the source contributes | 0x005A9F56, 0x005A9FD4; used 0x005A92F8, 0x005A9428 |
| +0x20 | `REMAP`; non-zero enables the color remap | 0x005AA0DA, 0x005AA10C; tested 0x005A8CED, 0x005A9171 |
| +0x24 | remap color 0 | 0x005A8D4A, 0x005A91C3 |
| +0x28 | remap color 1 | part of the same four |
| +0x2C | remap color 2 | part of the same four |
| +0x30 | remap color 3; the type-32 lane swap reads and rewrites it (0x005A8CA9..0x005A8CC3), but no blend consumes it | 0x005AA19F |

The type-32 path reads +0x00, +0x0C, +0x14, +0x20 and +0x24..+0x30, the
last of those only to swap its lanes. The type-8 path reads +0x00, +0x0C,
+0x10, +0x14, +0x18, +0x1C, +0x20 and +0x24..+0x2C; it never touches +0x04,
+0x08 or +0x30.

All three remap call sites pass a color count of 3 (`push 3` at
0x005A8D3B in the type-32 pixel loop, 0x005A91B6 in the type-8 pixel loop
and 0x005A93F5 in the type-8 palette merge), so the fourth remap color at
+0x30 never reaches a blend. Issue #20 recorded four `VINYL_COLOUR<n>`
slots with four sentinels; the fourth is gathered but not consumed here.

`NUMCOLOURS` is read from the part's attributes for records 1..4 and
defaults to 0x10 for records 0 and 5 (0x005A9FD4). `REMAP` is only looked
up for records 1..4 (`cmp ebx, 1` / `cmp [esp+0x10], 5` at
0x005AA0AC..0x005AA0BA), so the hood/spoiler vinyl and the doorline never
remap. The four remap colors are read from a per-layer attribute object;
when that object is missing, 0x005AA1A7 substitutes `0xFF << (8 * k)` for
color `k`, that is `0x000000FF`, `0x0000FF00`, `0x00FF0000`, `0xFF000000`.

## The request cache

The two calls the dispatcher makes before it composites are a memo on the
whole parameter block. Both take the block in `eax`.

`0x005A8400` reads the target's hash at `target+0x24`, subtracts
`bin_hash("DUMMY_SKIN1") = 0x530B82B0` (`add eax, 0xACF47D50` at
0x005A8409) and returns 0 at once if the difference is above 3
(0x005A840E). Otherwise it jumps through the table at 0x005A8478 and
compares the block against one of four caches with
`mov ecx, 0x6F` + `repe cmpsd`:

| Difference | Target | Cache |
| ---: | --- | --- |
| 0 | `DUMMY_SKIN1` | 0x00733738 |
| 1 | `DUMMY_SKIN2` | 0x007338F4 |
| 2 | `DUMMY_SKIN3` | 0x00733AB0 |
| 3 | `DUMMY_SKIN4` | 0x00733C6C |

It returns 1 when all 0x6F dwords match (`sete al` at 0x005A842B), and
0x005AA257 then skips compositing entirely while still reporting success.
`0x005A8490` is the same dispatch with `rep movsd` instead
(0x005A84B4), so it writes the block into that cache after a composite.

0x6F dwords is 444 bytes, which is exactly the parameter block:
`0x18 + 8 * 0x34 + 4 = 0x1BC`. That confirms the block layout above
independently, and it confirms that `target+0x24` holds the texture's name
hash: the four caches are the four `DUMMY_SKIN` textures that exist in
`Cars/TEXTURES.BIN` (see "Retail cross-checks").

The cache compares raw pointers as well as values, including the locked
pixel pointers of every layer, so it only skips work when the whole request
is byte-identical. It is a caller-level memo and not part of the pixel
arithmetic, so `nfsu` does not implement it.

A slot is invalidated by 0x005A8500, called from the texture-pack teardown
at 0x00545BAB. That call site guards on 0x530B82B0, B1 and B2
(0x00545B96, 0x00545B9D, 0x00545BA4) and not on B3, so the `DUMMY_SKIN4`
slot is never invalidated there. That is an observation, not a diagnosis.

## Shared arithmetic

### 0x00674898, float to int

`fistp qword` with the current rounding mode (0x006748A7), then a
residual-sign fixup at 0x006748BB..0x006748F5 that moves the result one step
toward zero whenever the rounding went away from zero: the positive branch
subtracts with `sbb` (0x006748EB), the negative branch adds with `adc`
(0x006748D3). The result is a plain truncation toward zero in every rounding
mode.

The conversion is 64-bit and the caller reads only the low dword
(0x006748B3), so a product past 2^31 wraps rather than saturating. A value
the 64-bit conversion cannot hold, and a NaN, make x87 store the integer
indefinite, whose low dword is zero. No weight either composite path builds
can reach that range, but `nfsu::skin_blend` is exported and reproduces it.

### 0x005A8640, weighted lane sum

Signature, cdecl: `u32 f(const u8* colors, const float* weights, int count,
int lane3_max)`. Arguments are at `[esp+0x2C]`, `[esp+0x30]`, `[esp+0x34]`
and `[esp+0x38]` after the prologue. The body is unrolled four times
(0x005A867E..0x005A8924) with an identical tail loop
(0x005A8940..0x005A89F5).

```text
acc0 = acc1 = acc2 = acc3 = 0
for i in 0 .. count-1:
    w = weights[i]
    if not (w > 0x3B808081f):        # 0x005A8681 and its three copies
        continue
    acc0 += trunc(colors[4i+2] * w)
    acc1 += trunc(colors[4i+1] * w)
    acc2 += trunc(colors[4i+0] * w)
    a    = trunc(colors[4i+3] * w)
    if lane3_max: acc3 = max(acc3, a & 0xFF)     # 0x005A86FB..0x005A8704
    else:         acc3 += a                      # 0x005A8719
lane0 = min(acc2, 255)   # 0x005A8A20..0x005A8A2C
lane1 = min(acc1, 255)   # 0x005A8A10..0x005A8A1C
lane2 = min(acc0, 255)   # 0x005A89FB..0x005A8A0C
lane3 = min(acc3, 255)   # 0x005A8A30..0x005A8A38
return lane0 | lane1<<8 | lane2<<16 | lane3<<24
```

Three details that a plain "weighted average" would get wrong:

- The skip test is `fcomp` against `0x006CC970` followed by
  `test ah, 0x41`, which is true for less, equal and unordered. A weight
  equal to the constant contributes nothing.
- Every product is truncated, never rounded.
- Only the upper clamp exists. The game never clamps at zero, and in
  `lane3_max` mode it masks the product to 8 bits before the comparison.

Each product is an exact integer in `0..255` times a float32, so a double
reproduces the x87 result exactly. `nfsu::skin_blend` implements this.

### Float constants

| Address | Bits | Value | Use |
| --- | --- | --- | --- |
| 0x006CC970 | 0x3B808081 | 0.0039215688 | lane value to weight, and the skip bound |
| 0x006CC7CC | 0x3C000000 | 0.0078125 | mask value to blend factor |
| 0x006CC7BC | 0x3F800000 | 1.0 | clamp |
| 0x006CC7A4 | 0x00000000 | 0.0 | clamp |
| 0x006CC858 | 0x4F800000 | 2^32 | unsigned `fild` fixup, never taken here |

`255 * 0x3B808081` rounds to exactly `1.0f`, so a full lane value becomes a
full weight.

### The mask value and its blend factor

Both paths reduce a mask texel to one byte, lane 0
(`and esi, 0xff` at 0x005A8CF2, `and edi, 0xff` at 0x005A9174). The type-32
path takes it straight from the mask surface; the type-8 path takes it from
the mask's palette entry for that index. Then:

| Mask | Behaviour | Evidence |
| --- | --- | --- |
| 0 | the target pixel is left untouched | 0x005A8D6F, 0x005A9220 |
| 1..127 | `result = blend({source, target}, {t, 1-t}, 2, sum)` | 0x005A8DD4, 0x005A9263 |
| 128..255 | the target pixel becomes the source outright | 0x005A8D6B, 0x005A91F2 |

`t = mask * 0.0078125`. The two paths then differ, although no mask value
can tell them apart:

- Type-32 (0x005A8D92..0x005A8DD4) forms `1-t` from the **unclamped** `t`
  (`fsub st(2)` at 0x005A8D9E), then clamps `t` above at 1.0
  (0x005A8DA4..0x005A8DB3) and `1-t` below at 0.0
  (0x005A8DBF..0x005A8DCC), separately.
- Type-8 (0x005A923C..0x005A926F) clamps `t` above first and then forms
  `1-t` from the clamped value. It has no lower clamp at all: 0x005A8E90
  contains no compare against 0x006CC7A4.

Because the branch only runs for `mask <= 127`, `t` never exceeds `127/128`
and every clamp is dead code. Both shapes are reproduced anyway. `t` and
`1-t` are exact multiples of `1/128`, so the blend is exactly reproducible.

There is no separate alpha rule. Lane 3 goes through the same weighted sum
as the other three, in summing mode, which for `t + (1-t) = 1` behaves like
the color lanes. On a fully opaque mask the source's lane 3 replaces the
target's.

### The color remap

Enabled per layer by record +0x20. For a source color `c` it computes

```text
w = { lane_a(c) * 0.0039215688,
      lane_b(c) * 0.0039215688,
      lane_c(c) * 0.0039215688 }
source = blend(record+0x24, w, 3, lane3_max = true)
```

with `(lane_a, lane_b, lane_c) = (2, 1, 0)` in the type-32 path
(0x005A8D04..0x005A8D33) and `(0, 1, 2)` in the type-8 path
(0x005A918A..0x005A91D7 and 0x005A93BB..0x005A9405). Those two orders are
each other's lane-0/lane-2 exchange, which is exactly the difference between
the two paths' color storage (see "Byte-lane order" below), so both feed the
same logical triple.

So a vinyl source is not a picture: its three low lanes are three weights
that mix three player-chosen colors, and its lane 3 selects the largest
scaled lane 3 of those colors rather than summing them.

## Byte-lane order

The two paths look for the same four marker colors but spell them
differently:

| k | Type-32, compared against target pixels (0x005A8AE5..0x005A8AFD) | Type-8, compared against target palette entries (0x005A8F8D..0x005A8FA5) |
| ---: | --- | --- |
| 0 | 0xBFFF0000 | 0xBF0000FF |
| 1 | 0xBF00FF00 | 0xBF00FF00 |
| 2 | 0xBF0000FF | 0xBFFF0000 |
| 3 | 0xBFFF00FF | 0xBFFF00FF |

Marker `k` in one list is the lane-0/lane-2 exchange of marker `k` in the
other, and the two that are symmetric under that exchange stay in place.
Three further facts agree with it:

- The type-32 path rebuilds the base color, the four special colors and a
  layer's remap colors as `(b3<<24) | (b0<<16) | (b1<<8) | b2` from the four
  stored bytes `b0..b3` (0x005A8B7E..0x005A8C07, 0x005A8C53..0x005A8CC3),
  which is the same lane-0/lane-2 exchange. The type-8 path stores the same
  five colors into palette entries 0..4 unchanged (0x005A90E1..0x005A90F8)
  and uses a layer's remap colors unchanged.
- The remap weight order is reversed between the paths, as shown above.
- The quantizer feed at 0x005A9581..0x005A95A1 packs its four output bytes
  in ascending lane order, matching the palette side.

Conclusion, purely from instructions: a color stored in a **surface** of the
type-32 path and the same color stored in a **palette** of the type-8 path
differ by exchanging lanes 0 and 2.

## Channel identity

The caller packs a layer's four remap colors from four named part
attributes, at 0x005AA134..0x005AA19F:

```text
ebp  = attribute("GLOSS")     # 0x005AA13C
ebp  = ebp<<8 | attribute("BLUE")    # 0x005AA153, 0x005AA158
ebp  = ebp<<8 | attribute("GREEN")   # 0x005AA16D, 0x005AA172
ebp  = ebp<<8 | attribute("RED")     # 0x005AA187, 0x005AA18C
store ebp at record +0x24 + 4*k      # 0x005AA19F
```

The string literals are at 0x006C3CE0, 0x006C7A28, 0x006C7A30 and
0x006C7A38, and the same four keys build the base color (`docs/CAR_SCENE.md`).
So in the **caller's order**, which is also the order the type-8 path stores
into its palette:

| Lane | Channel |
| ---: | --- |
| 0 | RED |
| 1 | GREEN |
| 2 | BLUE |
| 3 | GLOSS |

The type-32 path exchanges lanes 0 and 2, so in a type-32 **surface** a
color is `(GLOSS<<24) | (RED<<16) | (GREEN<<8) | BLUE`.

Two consequences follow, and both are consistent with the four marker
colors reading as red, green, blue and magenta in the same slot order in
both lists:

- The three remap weights are RED, GREEN and BLUE of the source color, in
  that order, in both paths: the type-32 path reads surface lanes 2, 1, 0
  and the type-8 path reads palette lanes 0, 1, 2, which are the same three
  channels. Lane 3, GLOSS, is never a weight.
- Both paths take lane 0 of the mask texel (0x005A8CF2, 0x005A9174), but
  that is a different channel in each: BLUE in a type-32 surface, RED in a
  type-8 palette entry. Retail masks carry the same value in lanes 0, 1 and
  2, so nothing distinguishes the two there.

What this does **not** establish: any pixel-format name, whether GLOSS is
consumed as an alpha channel by the renderer, or the byte order the
hardware finally sees.

## Target markers

Both paths keep one process-wide cache:

| Address | Contents |
| --- | --- |
| 0x00733E28 | 4 lists of 16 u32 pixel indices |
| 0x00733F4C | one-shot valid flag |
| 0x00733F50 | 4 u32 counts |

It is filled on the first composite of the process, from the untouched
target, and reused unchanged afterwards. Neither path checks the 16-entry
bound before writing (0x005A8B43, 0x005A9083), so a target with more than 16
pixels of one marker color would write past the list; `nfsu` refuses that
input instead.

Type-32, 0x005A8AC9..0x005A8B74: the target's 32-bit pixels are compared
against the four marker colors, first match wins, and the pixel index
(`byte offset >> 2`, 0x005A8B3E) is appended.

Type-8, 0x005A8F8D..0x005A90A1: each marker color is looked up in the
target's 256-entry palette first (index `0x100` means "absent", and the slot
keeps -1, so it can never match a 0..255 index). Then every 8-bit pixel that
carries a marker's palette index is appended and rewritten to `k+1`, and
every other pixel is rewritten to 0.

When the cache is already valid the type-8 path simply zeroes all
`width * height` index bytes (0x005A90B7..0x005A90CC), and the type-32 path
relies on the base-color fill.

At the very end both paths write the four special colors back over the
cached pixels: the type-32 path stores the color itself
(0x005A8E24..0x005A8E6E), the type-8 path stores the index `k+1`, which
entry `k+1` of the composited palette holds (0x005A9645..0x005A9685). The
marker fix-up runs after all layers, so a layer's result at a marker pixel
is always discarded.

## Type-32 path, 0x005A8A50

```text
if target == 0 or target[0x4A] != 0x20: return 0
lock the target                                   # 0x005A8AA1
    vtable slot +0x4C of *(*target + 0x18), args (this, 0, &out, 0, 0)
    every argument except the output pointer is zero
    out is two dwords; only out[1], the pixel base, is used (0x005A8AAC)
w = (i16)target[0x44]; h = (i16)target[0x46]      # 0x005A8AA4
if marker cache is empty: scan the w*h pixels     # 0x005A8AC9
exchange lanes 0 and 2 of the four special colors # 0x005A8B7E
fill w*h pixels with the base color, lanes exchanged  # 0x005A8C19
for each of the `count` layer records:
    if record+0x00 == 0: continue
    if record+0x20 != 0: exchange lanes 0 and 2 of the four remap colors
    src  = record+0x0C                            # 4 bytes per pixel
    msk  = record+0x14                            # 4 bytes per pixel
    for i in 0 .. w*h-1:
        c = src[i]
        m = msk[i] & 0xFF
        if record+0x20 != 0 and m != 0: c = remap(c)
        if   m > 127: dst[i] = c
        elif m != 0:  dst[i] = blend({c, dst[i]}, {t, 1-t}, 2, sum)
write the four special colors over the cached marker pixels
unlock the target                                 # vtable slot +0x50
return 1
```

Notes:

- The pixel walk is linear over `w*h` elements with no pitch, for the target
  and for every source and mask. The lock's first output dword is never
  read, so the game assumes tightly packed surfaces of equal size.
- The remap lane exchange happens in place in the caller's parameter block,
  once per layer. Applying it twice would restore the original, so a
  re-used block would be wrong; the caller rebuilds the block per call.
- The 2 MiB side table and the palette machinery of the type-8 path do not
  exist here: a partially masked pixel simply becomes a new 32-bit value.

## Type-8 path, 0x005A8E90

This path composes an **indexed** target, so it has to build a palette as
it goes. Nothing on the retail PC build reaches it: the target is format
code 0x20, and the texture upload rewrites every paletted texture's type
byte to 0x20 as well (see "Paletted textures never reach the type-8
path"). It is recovered from instructions alone.

```text
if target == 0 or target[0x4A] != 8 or [0x007301D4] == 0: return 0
lock the target the same way, pixel base = out[1] # 0x005A8EEB
pal = target[0x74]                                # 256 u32 entries
w = (i16)target[0x44]; h = (i16)target[0x46]
table = alloc(0x200000)                           # 0x005A8F06, 8-byte records
copy every present layer's source and mask pixel pointers into cursors
if marker cache is empty: find each marker's palette index, then re-index
    every pixel to k+1 or 0                       # 0x005A8F8D
else: zero all w*h index bytes                    # 0x005A90B7
pal[0] = base color; pal[1..4] = the four special colors, all unchanged
extra = 0
for each pixel i:
    cur = pal[index[i]]
    next = 5
    entry = none
    for each layer record with record+0x00 != 0:
        s = source_cursor[layer]++                       # one byte
        c = (record+0x10)[s]                             # source palette
        m = (record+0x18)[mask_cursor[layer]++] & 0xFF   # mask palette
        if record+0x20 != 0 and m != 0: c = remap(c)
        if m > 127:
            index[i] = (u8)(next + s)                    # 0x005A9204
            cur = c
            if entry: drop it, extra -= 1                # 0x005A9210
        elif m != 0:
            cur = blend({c, cur}, {t, 1-t}, 2, sum)
            if entry: entry.color = cur
            else: append {x = i % w, y = i / w, cur}, extra += 1
            index[i] = 0xFF                              # 0x005A92D1
        next += record+0x1C
used = 5
for every layer record, present or not:                  # 0x005A9360
    if record+0x20 == 0: copy record+0x1C entries of record+0x10 to pal[used..]
    else:                remap each of them into pal[used..]
    used += record+0x1C
if extra != 0:
    if extra < 255 - used:                               # 0x005A9455
        give every recorded pixel its own palette entry at used, used+1, ...
    else:
        quantize the recorded colors to 255-used entries and re-map each
        pixel   # see "The quantizer fallback" below
write k+1 over the cached marker pixels                  # 0x005A9645
unlock the target
return 1
```

The composited palette therefore has a fixed shape:

| Entry | Contents |
| --- | --- |
| 0 | base paint color |
| 1..4 | the four special colors, in parameter-block order |
| 5.. | every layer's source palette, in layer order, remapped if the layer remaps |
| .. | one entry per partially blended pixel, or the quantized replacements |
| 255 | never allocated: `0xFF` is the "not yet resolved" pixel marker |

Bounds the game does not check:

- The palette merge writes `5 + sum(record+0x1C)` entries into a 256-entry
  palette with no bound test. `nfsu` refuses anything past 256. Ending
  exactly at 256 is legal and accepted, because 0x005A9446 leaves before
  the stage that reserves index 255 whenever nothing was blended.
- The merge walks every record, while the per-pixel loop skips records whose
  +0x00 is zero. An absent record that still declared entries would
  desynchronise the two; `nfsu` refuses that input.
- The side table is a fixed `0x200000` bytes of 8-byte records, so the path
  is only sound for at most 262144 pixels. `nfsu` refuses more. The retail
  target is 512x512, which reaches that bound exactly.
- `index[i] = next + s` is a byte add (`add dl, bl` at 0x005A9204) and `s`
  is a full 0..255 byte that `NUMCOLOURS` does not bound, so it can wrap and
  can even land on the 0xFF sentinel. `nfsu` **reproduces** this rather than
  refusing it, because it is ordinary in-range behaviour of a byte add.

## Paletted textures never reach the type-8 path

The texture upload at 0x00407DB0 walks the same records the compositor
later reads. When a record's format value is 0x29 (0x00407E00) it
allocates `[ti+0x38] * 4` bytes (0x00407E21), expands every level one byte
at a time through the 256-entry palette at `[ti+0x74]`

```text
movzx ecx, byte ptr [ebp]      # 0x00407E70, the index
mov   edx, [esp+0x24]          # the palette
mov   ecx, [edx + ecx*4]       # the entry, copied whole
mov   [edi], ecx               # 0x00407E7B, one 32-bit pixel
```

then sets the format to 0x15 (0x00407EA8) and writes
`mov byte ptr [esi+0x4A], 0x20` (0x00407EB1).

Three things follow:

- Byte +0x4A of the texture object is the pack record's format code at +74,
  in place, but it is rewritten for every paletted texture. Every vinyl and
  every vinyl mask therefore arrives at the compositor as a 0x20 texture
  with 32-bit pixels and no palette, which is why the type-32 path reads
  four bytes per source and mask element.
- The expansion copies a palette entry into a pixel **whole**, so an
  expanded surface has exactly the byte-lane order the palette had on disk.
- The layer record's palette fields at +0x10 and +0x18 are only written
  when the target's type byte is not 0x20 (0x005AA054, 0x005AA08B), so on
  this build they are never written at all.

## The quantizer fallback

When the partially blended pixels do not fit in the free tail of the
palette, the type-8 path reduces them instead
(0x005A94B6..0x005A962F). The five routines involved are a four-channel
port of Anthony Dekker's NeuQuant neural-net quantizer, identified by its
constants rather than by any string:

| Address | Role | Deciding constants |
| --- | --- | --- |
| 0x005A7B70 | initnet | `freq[i] = 0x10000 / netsize` (0x005A7BAC), `network[i][c] = (i<<12)/netsize` |
| 0x005A81B0 | learn | initial alpha `1<<10` (0x005A8235), radius bias shift 6 (0x005A8228), 100 cycles (0x005A81F7), primes 499/491/487/503 with 4-byte pixels (0x005A8282..0x005A82B5), `samplepixels = lengthcount / (4 * samplefac)` (0x005A81EA) |
| 0x005A7BF0 | unbiasnet | `network[i][c] >>= 4` (0x005A7C10..0x005A7C1B), `network[i][4] = i` |
| 0x005A7C30 | inxbuild | sort on channel 1, 5-dword records, 256-entry index at 0x007316A0 |
| 0x005A7D60 | inxsearch | L1 distance over all four channels, seeded from the channel-1 index |

`learn` scales each pixel lane by `<<4` (0x005A82E0..0x005A82EF, netbias
shift 4). The palette write-back at 0x005A9581..0x005A95A1 packs the four
network channels back in ascending lane order, so the lane mapping is the
identity throughout.

The reference in this repository refuses this case rather than
reimplementing the quantizer, because reproducing it bit for bit would need
its own evidence and its own vectors.

## Mip generation

Both paths lock once, composite the surface they get back and unlock it.
The lock call passes zero for every argument except the output pointer
(0x005A8A95..0x005A8AA1, 0x005A8EDC..0x005A8EEB), and neither routine
contains a downsample loop or a second lock. The same holds one level up:
each layer's source and mask are locked and unlocked with the same
all-zero arguments (0x005AA04E, 0x005AA085, 0x005AA2A0, 0x005AA2B7), the two
pre-passes at 0x005AA250 and 0x005AA25D only compare and copy the parameter
block, the tail of 0x005A9CE0 only unlocks, and the only caller, 0x0058BE57,
sets a flag and returns. Nothing in this call graph regenerates a mip chain.

The retail data agrees: the target the game actually composites into,
`DUMMY_SKIN1`, is stored with a mip level count of 1, so there is no chain
to regenerate. See "Retail cross-checks".

## Retail cross-checks

All files below were read only, and their SHA-256 values were taken before
and after the work and are unchanged. The installation they come from is
not pristine: it carries third-party loader files, `FrontEnd/FRONTA.BUN` is
0 bytes in it, and `Global/GLOBALA.BUN` has a recent modification time. The
`speed.exe` in it still hashes to the reference value above, and every
claim below names the file and offset it came from, so a different copy can
be checked against it.

| File | SHA-256 |
| --- | --- |
| `Cars/TEXTURES.BIN` | `999EC47CC816CADA2158317A46E00023832031B3A4A35DDCBA31F5FBD2F9EB58` |
| `Cars/240SX/TEXTURES.BIN` | `725DC56CB4807FF8E8E96F1D667F5FDF8DC5ADE3C7D06E759FC62E7DEEFDF6BA` |
| `Cars/240SX/VINYLS.BIN` | `46099AF77F100C3E09EBDBF4813631FFE3188CDE411F79D570C01B076E4EA1E3` |
| `Global/GLOBALB.BUN` | `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9` |
| `Global/GLOBALA.BUN` | `1502D9305F37E51503D4C4E57CAE32890059497AC450FEEE6D72B50323568953` |

### The marker colors exist, in the target, verbatim

`bin_hash("DUMMY_SKIN1") = 0x530B82B0` resolves to texture index 120 of the
pack inside `Cars/TEXTURES.BIN` (info record at 24944, name string at
24956, JDLZ blob at 484456). `DUMMY_SKIN2`, `3` and `4` follow it at
`0x530B82B1..B3`. Every one of the four is 512 x 512 with **one** mip
level, info format code 0x20 and D3DFORMAT 21.

Decoded level 0 of `DUMMY_SKIN1` holds only seven distinct 32-bit values:

| Value | Pixels | Share |
| --- | ---: | ---: |
| 0xBF000000 | 261938 | 99.921% |
| 0xBF000100 | 141 | 0.054% |
| 0xBFFF00FF | 16 | 0.006% |
| 0xBF0000FF | 16 | 0.006% |
| 0xBF00FF00 | 16 | 0.006% |
| 0xBFFF0000 | 16 | 0.006% |
| 0xBF010101 | 1 | 0.000% |

The four marker colors sit in four 4 x 4 blocks in the bottom-right corner,
all on rows 508..511: magenta at columns 496..499, blue at 500..503, green
at 504..507 and red at 508..511. Read together with the slot table, the red
block is the engine's paint, green the spoiler's, blue the brakes' and
magenta the exhaust's. `DUMMY_SKIN2..4` are identical. This settles four
things at once:

- The marker colors are real, and the compositor reads them rather than
  writing them.
- They match the **type-32** list at 0x005A8AE5..0x005A8AFD, read as
  little-endian u32. Reading the same pixels byte-reversed matches nothing,
  anywhere in the scanned packs.
- Sixteen pixels per marker is exactly the 16-entry capacity of each cached
  list at 0x00733E28. The lists are sized for this texture.
- The retail PC target has format code 0x20, so the retail PC path is the
  type-32 one. Nothing was found that drives the type-8 path on PC.

None of the four values occurs in any of the 223232 palette entries of
`Cars/240SX/VINYLS.BIN`, so a vinyl palette can never be mistaken for a
marker. `DUMMY_WHEEL1..4` (0xBD97DD90..93, 128 x 128) contain none of them
either, and no other A8R8G8B8 record in `Cars/240SX/TEXTURES.BIN`,
`Cars/TEXTURES.BIN`, `Global/GLOBALA.BUN` or `Global/GLOBALB.BUN` does.

### The mask encoding matches the 1/128 scale

The three car-local doorline masks are 512 x 512 with 7 levels:

| Hash | Name | Level-0 lanes 0..2 | Level-0 lane 3 |
| --- | --- | --- | --- |
| 0x699B6846 | `240SX_DOORLINE_MASK` | 0..128, 128 distinct values | constant 0xFF |
| 0xB13AC2ED | `240SX_DOORLINE_KIT_MASK` | 0..128, 128 distinct values | constant 0xFF |
| 0xBFD2E2FC | `240SX_DOORLINE_WIDEBODY_MASK` | 0..128, 128 distinct values | constant 0xFF |

Lanes 0, 1 and 2 are bit-identical in 262140, 262134 and 262140 of the
262144 pixels, the exceptions differing by one, so a mask is one coverage
image replicated across the color lanes with the fourth lane forced opaque.
The compositor reads only lane 0, which is consistent with that.

The decisive part is the **range**: every value from 0 to 128 occurs and
nothing above 128 does, and 0x80 is the dominant non-zero value, on 7.15%
to 7.41% of pixels. That is exactly the encoding the arithmetic expects.
`t = mask / 128` covers 0 to 1 in 1/128 steps, and 128 is the first value
the `mask > 127` test treats as a full replacement. A 0..255 mask would
leave half of the lerp range unreachable.

`0xBFD2E2FC` is `bin_hash("240SX_DOORLINE_WIDEBODY_MASK")`; the 24-byte
name field truncates the string, which is why two records appear to share a
name.

The three doorline sources themselves are the opposite shape: lanes 0..2
are exactly zero on 94% to 96% of pixels and the variation is in lane 3,
which ranges 0x3B to 0xFF and is never zero.

### The vinyl palettes match the three-weight remap

`Cars/240SX/VINYLS.BIN` holds 436 base palettes, 402 with 16 entries and 34
with 64, and 436 masks. Classifying a base palette's lanes by whether any
entry exceeds 64:

| Lanes used | Palettes |
| --- | ---: |
| lane 2 only | 169 |
| lanes 1 and 2 | 103 |
| lanes 0, 1 and 2 | 108 |
| lanes 0, 1, 2 equal, that is grey | 56 |

Lane 2 is never all-zero in any of the 402 16-entry palettes, and using
lane 0 implies using lanes 1 and 2. Lane 3 is never all-zero and is zero
exactly on each palette's empty entry.

The part database in `GLOBALB.BUN` carries a key `0x6212682B =
bin_hash("NUMREMAPCOLOURS")` on the same 534 records that carry `REMAP`,
`NUMCOLOURS` and `TEXTURE`. Cross-referencing it against the lanes each
texture actually uses, over the 433 records that resolve to the 240SX pack:

| NUMREMAPCOLOURS | lane 2 only | lanes 1, 2 | lanes 0, 1, 2 |
| ---: | ---: | ---: | ---: |
| 1 | 168 | 8 | 37 |
| 2 | 0 | 93 | 10 |
| 3 | 0 | 2 | 27 |

So the first remap color is driven by lane 2, the second adds lane 1 and
the third adds lane 0. In a type-32 surface lane 2 is RED, lane 1 GREEN and
lane 0 BLUE, and the weights the path builds are lanes 2, 1, 0 in that
order. The first weight is therefore the first remap color, as the
instructions require, and the hard-coded count of 3 is safe: an unused lane
is zero, its weight is zero, and 0x005A8681 drops it.

`NUMCOLOURS` is 16 on 494 records and 64 on 40, matching the two vinyl
format codes 0x80 and 0x81 exactly. That confirms layer record +0x1C as the
source palette's entry count.

### The remap colors are installed parts

The remap loop reads `RideInfo + 0x4F4 + (index - 1) * 16 + j * 4`
(0x005AA126, 0x005AA134). `RideInfo + 0x44C` is the 120-entry part table,
so `0x4F4` is slot 42, and the slot-name table at 0x006F0970 gives:

| Slot | Name |
| ---: | --- |
| 30 | `BASE_PAINT` |
| 31..34 | `VINYL_LAYER0`..`VINYL_LAYER3` |
| 35, 36 | `VINYL_HOOD`, `VINYL_SPOILER` |
| 37..41 | `PAINT_ENGINE`, `PAINT_SPOILER`, `PAINT_BRAKE`, `PAINT_EXHAUST`, `PAINT_RIM` |
| 42..57 | `VINYL_COLOUR0_0` .. `VINYL_COLOUR3_3` |

Layer record `L` in 1..4 therefore takes its four remap colors from
`VINYL_COLOUR<L-1>_0` .. `_3`, in that order, and `_3` is never read.

In `GLOBALB.BUN` those slots are filled with the `VINYL_L1_COLOR01..10`,
`VINYL_L2_COLOR01..20` and `VINYL_L3_COLOR01..30` swatch records. Each
carries four separate attributes rather than a packed color:

| Record | RED | GREEN | BLUE | GLOSS |
| --- | ---: | ---: | ---: | ---: |
| `VINYL_L1_COLOR01` | 5 | 5 | 5 | 100 |
| `VINYL_L1_COLOR02` | 250 | 250 | 250 | 100 |
| `VINYL_L2_COLOR01` | 130 | 130 | 155 | 100 |
| `CUSTOMPAINT_1` | 128 | 128 | 128 | 128 |
| `GLOSS_L1_COLOR01` | 130 | 130 | 142 | 128 |

Over all 1755 `RED`, `GREEN`, `BLUE` and `GLOSS` attribute values the range
is 0..255 and no value has a byte above the low one set, so the packing at
0x005AA13C..0x005AA19F is the only place the four become one dword. The key
hashes are `RED` 0x0000D99A, `GREEN` 0x02DDC8F0, `BLUE` 0x00136707,
`GLOSS` 0x02DAAB07, `REMAP` 0x039DD714, `NUMCOLOURS` 0x564B8CB6,
`NUMREMAPCOLOURS` 0x6212682B and `TEXTURE` 0xFD35FE70.

No record in `GLOBALB.BUN` is literally named `BASE_PAINT`. `BASE_PAINT` is
slot 30, and the records installed into it belong to the `GLOSS`, `METAL`
and `PEARL` families.

### What the retail data does not decide

- Whether an 8-bit source palette ever reaches the type-8 path in the
  mirrored lane order that path expects. On disk both the A8R8G8B8
  surfaces and the P8 palettes store a color with lane 0 BLUE and lane 2
  RED, which is the type-32 order. Since the retail PC target is format
  code 0x20, the type-8 path never runs on this data, and nothing here says
  what a build that did run it would hand the path.
- Whether the alpha value 0xBF in `DUMMY_SKIN1` means anything. The
  doorline sources use 0x3B for their background, so there is no second
  data point.
- The 37 records whose `NUMREMAPCOLOURS` is 1 but whose texture still uses
  all three lanes. The compositor would weight all three remap colors for
  them; nothing says whether that is intended.
- `FrontEnd/FRONTA.BUN` is 0 bytes in this installation and `FrontB.lzc`
  uses a compression this project does not decode, so no front-end texture
  was examined. No front-end `.BIN` contains the `DUMMY_SKIN` hash or the
  string.

## What this repository implements

`include/nfsu/body_skin.h` and `src/body_skin.cpp` hold a bounded CPU
reference for exactly the instruction-backed behaviour above:

- `skin_blend` for 0x005A8640, including the skip bound, the truncation, the
  lane-3 mode and the single upper clamp.
- `skin_swap_lane02` for the lane exchange the type-32 path applies.
- `composite_body_skin_32` for 0x005A8A50 and `composite_body_skin_8` for
  0x005A8E90, with the marker cache passed in and out instead of living in
  a global.

It deliberately does **not** implement:

- the color quantizer at 0x005A7B70 / 0x005A81B0 / 0x005A7BF0 / 0x005A7D60
  that the type-8 path falls back to when the blended pixels no longer fit;
  that input is refused with a message instead of guessed;
- any pixel-format name and any mip generation. The API numbers byte lanes
  rather than naming channels, because the two paths disagree about the
  order; the channel each lane carries is stated once, above, with its
  evidence;
- the request cache at 0x005A8400 and 0x005A8490, which is a caller-level
  memo over the whole parameter block and not pixel work;
- the selection of the inputs, which is issue #20's subject and lives in
  `docs/CAR_SCENE.md`.

It also differs from the game in four places that are recorded rather than
hidden:

- `skin_blend` is exported with only its count bounded. For a weight far
  outside anything the composite paths build, a product past 2^31 wraps the
  same way the game's does. A product past 2^63 yields zero, the low dword
  of the x87 integer indefinite the game would store. A NaN weight is
  skipped by the unordered comparison before conversion.
- The type-8 output palette is zero-filled, so entries at or above
  `palette_used` are meaningless. The game composites into the target's own
  palette and leaves whatever was there before those entries.
- The quantizer fallback is refused, so a target needing it produces no
  result at all rather than an approximate one.
- Every other bound the game leaves unchecked is refused, which the game
  would instead walk off the end of.

Both composite entry points leave their output object untouched on refusal
and report why. The lower-level `skin_blend` helper resets its scalar result
to zero before validating its input.

`tests/body_skin_tests.cpp` pins each operation with vectors whose expected
values were worked out from the instructions by hand before the code ran.
One of them, the default-remap identity, disagreed with the first hand
calculation and was corrected in the test, not in the code: a lane value of
1 produces a weight exactly equal to the skip bound and contributes
nothing.

As a separate check, both entry points were compared against second
implementations written independently from the same instruction listing,
on randomly generated cases with the mask values biased towards 0, 1, 127,
128 and 255:

| Path | Seeds | Cases | Result |
| --- | ---: | ---: | --- |
| type-32, one pixel in four a marker | 6 | 1800 | all identical |
| type-8, planted marker palette entries | 6 | 1800 | all identical |

The comparison was re-run unchanged after an adversarial review led to the
arithmetic being reworked, and all 3600 cases still agree. Those programs
are throwaway scratch, not part of this repository, so the check is
recorded here rather than automated.

## Observed, inferred, unknown

### Observed in the executable

Everything in the sections above that carries an address.

### Inferred

- The lock's two-dword output is a pitch followed by a pixel pointer. Only
  the second dword is read, so the first is an inference from the shape of
  the call, not from the compositor.
- The five routines at 0x005A7B70, 0x005A7BF0, 0x005A7C30, 0x005A7D60 and
  0x005A81B0 are a four-channel NeuQuant. The constants make this as close
  to certain as an identification without symbols gets, but it is still an
  identification, not a transcription.
- `0x007301D4` gates the whole type-8 path (0x005A8EBF) and also selects
  the quantizer budget at 0x005A94C0. It is non-zero whenever the path
  runs, so the budget comparison at 0x005A94C9 always passes for a target
  of at most 262144 pixels.

### Unknown

- Any pixel-format name for either path, and whether the renderer consumes
  `GLOSS` as an alpha channel. The channel names come from the attribute
  keys the caller reads, not from a format declaration.
- What the type-8 path is for. It is complete and self-consistent, but on
  this build the target is format code 0x20 and every paletted texture is
  expanded before it could be a source, so nothing reaches it.
- Whether a lock leaks when a mask lock fails. 0x005AA0A7 abandons the
  record after the source was already locked at 0x005AA051, and the
  cleanup loop at 0x005AA283 reads the record the cursor did not advance
  past. Traced, not observed.
- Whether both target types can occur in one process. The marker cache at
  0x00733E28 is shared through the single flag 0x00733F4C, so whichever
  path runs first decides the cached pixel indices for both. Nothing was
  traced that would prevent that, and nothing was traced that shows it
  happening.
