# Siphon

GameCube disc and asset tooling.

Reads ISO, CISO, GCZ, WIA, RVZ, and WBFS images, plus RARC archives. Has YAZ0
decompression and a `dol split` replacement compatible with dtk-template
project configs.

## Build

```sh
cmake -S . -B build -DBUILD_SIPHON_BINARY=ON
cmake --build build
```

`BUILD_SIPHON_BINARY=ON` builds the `siphon` CLI. Default (off) builds it as a
library only.

## CLI

```
siphon disc <image> <outdir> [--expect-id ID]
siphon arc  <archive> <outdir>             extract all
siphon arc  ls <archive>                   list entries
siphon arc  cp <archive>:<inner> <out>     extract one entry
siphon dol  split <config.yml> <outdir>
siphon yaz0 decompress <in> -o <out>
```

## Dependencies

- zlib
- zstd (optional, for ZSTD-compressed WIA/RVZ)
- libyaml

## License

MIT.
