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
3. Install `libass` with `vcpkg` for the platform you want to build. For example:
   ```
   vcpkg install libass:x86-windows-static
   vcpkg install libass:x64-windows-static
   vcpkg install libass:arm64ec-windows
   ```
4. Make vcpkg discoverable before building. Set `VCPKG_ROOT`, pass `VcpkgRoot` as an MSBuild property, or run `vcpkg integrate install`.
5. Open `VSFilter.sln` with Visual Studio 2022 and build the target you want.

Notes:
- The project uses `vcpkg` for `libass`; the solution no longer builds the `SMP` libass dependency chain.
- The default triplets are `x86-windows-static`, `x64-windows-static`, and `arm64ec-windows`. If you prefer another triplet, override `VcpkgTriplet`.
- `Win32` and `x64` use static triplets by default. When using a dynamic triplet, such as the default `ARM64EC` triplet, remember to deploy `libass.dll` and any dependent DLLs together with the built filter.

## Note
This project (XySubFilter with libass) is a **variant** of XySubFilter, which uses [libass](https://github.com/libass/libass) to render **SSA/ASS**, so
1. This project may have some compatibility issues with XySubFilter when rendering SSA/ASS, which is largely dependent on the libass ( of course, we welcome issues, if you don't know whether this comes from libass or from itself )
2. As for the rest of the project ( such as srt, sup and other basic parts ), I basically left it unchanged, so basically it inherits all the strengths and weaknesses of the original project
