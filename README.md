## What is this
This is a subtitle render filter for directshow video players such as mpc-hc/mpc-be, potplayer with madVR, which use libass to render ASS and SSA format subtitles, and origns from https://github.com/Cyberbeing/xy-VSFilter/pull/16, named XySubFilter with libass.

## How to use
1. Run Install_XySubFilter.bat as administrator and dont move or delete the "XySubFilter.dll".
    - Alternatively, you can install [XySubFilter](https://scoop.sh/#/apps?q=xysubfilter-libass-np&s=0&d=1&o=true) via Scoop:
    ```
    scoop bucket add nonportable
    scoop install xysubfilter-libass-np -g
    ```
3. Select XySubFilter in your player as your subtitle renderer.
    - Options -> Subtitle -> Subtitle renderer in MPC-BE 
    - Options -> Playback -> Output -> Subtitle Renderer in MPC-HC

## How to compile
1. Clone or download release from https://github.com/ShiftMediaProject/VSYASM and https://github.com/ShiftMediaProject/VSNASM, and run install_script.bat as administrator.
2. Clone the repo and run `git submodule update --init --recursive --remote` in the folder.
3. In Visual Studio, manully set the Runtime Library of libass and its dependencies to "/MT" for Release and "/MTd" for Debug. See [here](https://docs.microsoft.com/en-us/cpp/build/reference/md-mt-ld-use-run-time-library?view=msvc-170) for more.
4. Build project xy_sub_filter.

### ARM64EC
`ARM64EC` is supported through a dedicated solution/platform configuration.

1. Open `VSFilter.sln` with Visual Studio 2022.
2. Select `ARM64EC` as the platform.
3. Install `libass` with vcpkg for an ARM64EC triplet, for example:
   ```
   vcpkg install libass:arm64ec-windows
   ```
4. Set `VCPKG_ROOT` in your environment before building, or pass `VcpkgRoot` as an MSBuild property.

Notes:
- `ARM64EC` builds default to `UseVcpkgLibass=true`, so the solution will no longer try to build SMP's `libass` project for that platform.
- The default triplet is `arm64ec-windows`, which links against vcpkg's dynamic `libass`. If you prefer another triplet, override `VcpkgTriplet`.
- When using a dynamic triplet, remember to deploy `libass.dll` and any dependent DLLs together with the built filter.

## Note
This project (XySubFilter with libass) is a **variant** of XySubFilter, which uses [libass](https://github.com/libass/libass) to render **SSA/ASS**, so
1. This project may have some compatibility issues with XySubFilter when rendering SSA/ASS, which is largely dependent on the libass ( of course, we welcome issues, if you don't know whether this comes from libass or from itself )
2. As for the rest of the project ( such as srt, sup and other basic parts ), I basically left it unchanged, so basically it inherits all the strengths and weaknesses of the original project
