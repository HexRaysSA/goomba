# Building

## Dependencies

gooMBA requires IDA SDK (8.2 or later) and the [z3 library](https://github.com/Z3Prover/z3).

## Building gooMBA with IDA SDK's make system

1. After unpacking and setting up the SDK, copy goomba source tree under SDK's `plugins` directory, 
for example `C:\<idasdk>\plugins\goomba`.

2. Download and extract [z3 build for your OS](https://github.com/Z3Prover/z3/releases) into the `z3` subdirectory, for example:

`z3/z3-4.11.2-x64-win`

 Under it, you should have `bin` and `include` directories

 Alternatively, set `Z3_BIN` and `Z3_INCLUDE` to point to the directories elsewhere.

3. If using another `z3` version than 4.11.2, open `makefile` and adjust

 * `Z3VER` with the correct version number
 * possibly, `Z3BIN` with the correct glibc version number (on Linux),
   or osx version number (on macOS)

4. build the necessary version of gooMBA, for example:

```make -j```  for 32-bit IDA
```make __EA64__=1 -j```  for IDA64

## Building gooMBA with ida-cmake

Alternatively, you can also build `gooMBA` with [ida-cmake](https://github.com/0xeb/ida-cmake).
Grab `ida-cmake` and clone it as per its instructions to the `<idasdk>` folder, then configure it properly.

Note: `gooMBA` source code is no longer required to be cloned or copied into `<idasdk>/plugins` in order to build it.

Make sure you have Z3 libraries deployed correctly in a folder of your choice (or in `<goomba>/z3`).

Now, configure CMake accordingly. In the `<goomba>` folder, run:

```bash
mkdir build
cmake .. -DZ3_DIR=<z3_dir>
```

On MS Windows, configure `cmake` with `-A x64`:

```bash
cmake .. -A x64 -DZ3_DIR=<z3_dir>
```

Note: if `Z3_DIR` is not specified, it will default to `<goomba>/z3`.

Then build with:

```
cmake --build .
```

For ida64 builds, specify the additional `-DEA64=YES` switch in the configuration step:

```bash
mkdir build64
cd build64
cmake .. -DEA64=YES -DZ3_DIR=<z3_dir>
```

# Deploying

To deploy the plugin outside the SDK and use it with your installed IDA, then copy the generated plugin binary from SDK's bin directory to your IDA install (or [user directory](https://hex-rays.com/blog/igors-tip-of-the-week-33-idas-user-directory-idausr/)):

On Windows:

 * `C:\<idasdk>\bin\plugins\goomba*` -> `C:\Program Files\IDA Pro <x.y>\plugins\`
 * `C:\<idasdk>\bin\cfg\goomba.cfg` -> `C:\Program Files\IDA Pro <x.y>\cfg\`
 * `C:\<idasdk>\bin\libz3.*` -> `C:\Program Files\IDA Pro <x.y>\`

On linux: 

 * `/path/to/<idasdk>/bin/plugins/goomba*` -> `/path/to/ida<xy>/plugins/`
 * `/path/to/<idasdk>/bin/cfg/goomba.cfg` -> `/path/to/ida<xy>/cfg/`
 * `/path/to/<idasdk>/bin/libz3.*` -> `/path/to/ida<xy>/`

On macOS:

 * `/path/to/<idasdk>/bin/plugins/goomba*` -> `/path/to/ida<xy>/ida.app/Contents/MacOS/plugins/`
 * `/path/to/<idasdk>/bin/cfg/goomba.cfg` -> `/path/to/ida<xy>/ida.app/Contents/MacOS/cfg/`
 * `/path/to/<idasdk>/bin/libz3.*` -> `/path/to/ida<xy>/ida.app/Contents/MacOS/`
 * `/path/to/<idasdk>/bin/libz3.*` -> `/path/to/ida<xy>/ida64.app/Contents/MacOS/`

(`x` and `y` are the major and minor version numbers of IDA, respectively)