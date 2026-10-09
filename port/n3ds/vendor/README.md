# Bundled audio decoder

`stb_vorbis.c` is Sean Barrett's Ogg Vorbis decoder, version 1.22, from
[nothings/stb](https://github.com/nothings/stb/blob/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_vorbis.c).
The pinned source has SHA-256
`4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636`.
Its license text is at the end of the source file; this build uses the public
domain dedication option. The file is compiled with file I/O and push-data
APIs disabled, and reads validated cache sample buffers in memory.
