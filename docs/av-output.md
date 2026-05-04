# AV Output Notes

SF2000-family boards expose a CVBS/AV output path through the HCRTOS display driver. The relevant old-HCRTOS evidence is:

- `board/hc15xx/common/dts/*`: `de-engine` supports `dual-output`, `cvbs-output`, and `tvtype`.
- `components/hc-examples/source/dis_test.c`: the SD output path sets `DIS_TYPE_SD`, maps PAL/NTSC-like modes to `TV_PAL` or `TV_NTSC`, calls `DIS_SET_TVSYS`, then registers a CVBS DAC with `DIS_REGISTER_DAC`.
- `hcuapi/dis.h`: public display ioctls include `DIS_SET_TVSYS`, `DIS_REGISTER_DAC`, and `DIS_UNREGISTER_DAC`.
- `hcuapi/tvtype.h`: `TV_NTSC = 1` and `TV_PAL = 0`.

UniFrog keeps the device tree enabled for dual output and CVBS:

```dts
#define CONFIG_DUAL_OUTPUT_SUPPORT 1
#define CONFIG_CVBS_OUTPUT_SUPPORT 1
...
DE: de-engine {
    dual-output = <CONFIG_DUAL_OUTPUT_SUPPORT>;
    cvbs-output = <CONFIG_CVBS_OUTPUT_SUPPORT>;
};
```

Runtime control lives in `unifrog_av_set_mode()`:

1. Open `/dev/dis`.
2. Always unregister the existing SD CVBS DAC first.
3. For NTSC or PAL, set `DIS_TYPE_SD` with `DIS_SET_TVSYS`.
4. Register CVBS DAC 0 with `DIS_REGISTER_DAC`.

Supported frontend modes:

- `Off`: unregister CVBS DAC.
- `NTSC`: SD output, `TV_NTSC`, interlaced.
- `PAL`: SD output, `TV_PAL`, interlaced.

This deliberately avoids direct register writes. The LCD path and SF2000-specific LCD rotation/MADCTL setup remain owned by the SDK/display layer.
