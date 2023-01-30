
# Bulding gooMBA

## dependencies

gooMBA requires IDA SDK (8.2 or later) and the [z3 library](https://github.com/Z3Prover/z3).

## Building

1. After unpacking and setting up the SDK, copy goomba source tree under SDK's `plugins` directory, 
for example `C:\idasdk_pro82\plugins\goomba`.

2. Download and extract [z3 build for your OS](https://github.com/Z3Prover/z3/releases) into the `z3` subdirectory:

 Under it, you should have `bin` and `include` directories.

 Alternatively, set `Z3_BIN` and `Z3_INCLUDE` to point to the directories elsewhere.

3. build the necessary version of gooMBA, for example:

```make -j```  for 32-bit IDA
```make __EA64__=1 -j```  for IDA64

4. Copy generated files from SDK's bin directory to your IDA install (or [user directory](https://hex-rays.com/blog/igors-tip-of-the-week-33-idas-user-directory-idausr/)):

On Windows:

 * `C:\idasdk_pro82\bin\plugins\goomba*` -> `C:\Program Files\IDA Pro 8.2\plugins\`
 * `C:\idasdk_pro82\bin\cfg\goomba.cfg` -> `C:\Program Files\IDA Pro 8.2\cfg\`
 * `C:\idasdk_pro82\bin\libz3.*` -> `C:\Program Files\IDA Pro 8.2\`

On linux: 

 * `/path/to/idasdk_pro82/bin/plugins/goomba*` -> `/path/to/ida82/plugins/`
 * `/path/to/idasdk_pro82/bin/cfg/goomba.cfg` -> `/path/to/ida82/cfg/`
 * `/path/to/idasdk_pro82/bin/libz3.*` -> `/path/to/ida82/`

On macOS:

 * `/path/to/idasdk_pro82/bin/plugins/goomba*` -> `/path/to/ida82/ida.app/Contents/MacOS/plugins/`
 * `/path/to/idasdk_pro82/bin/cfg/goomba.cfg` -> `/path/to/ida82/ida.app/Contents/MacOS/cfg/`
 * `/path/to/idasdk_pro82/bin/libz3.*` -> `/path/to/ida82/ida.app/Contents/MacOS/`
 * `/path/to/idasdk_pro82/bin/libz3.*` -> `/path/to/ida82/ida64.app/Contents/MacOS/`

