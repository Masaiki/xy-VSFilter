## What is this
This is a subtitle render filter for directshow video players such as mpc-hc/mpc-be, potplayer with madVR, which use libass to render ASS and SSA format subtitles, and origns from https://github.com/Cyberbeing/xy-VSFilter/pull/16, named XySubFilter with libass.

## How to use
1. Run Install_XySubFilter.bat or Install_VSFilter.bat as administrator and dont move or delete the registered DLL.
    - Alternatively, you can install [XySubFilter](https://scoop.sh/#/apps?q=xysubfilter-libass-np&s=0&d=1&o=true) via Scoop:
    ```
    scoop bucket add nonportable
    scoop install xysubfilter-libass-np -g
    ```
3. Select XySubFilter in your player as your subtitle renderer.
    - Options -> Subtitle -> Subtitle renderer in MPC-BE 
    - Options -> Playback -> Output -> Subtitle Renderer in MPC-HC

## How to compile
1. Clone or download release from https://github.com/ShiftMediaProject/VSYASM and run install_script.bat as administrator.
2. Install dependencies with vcpkg:
    ```
    .\vcpkg install libass boost-flyweight boost-smart-ptr log4cplus[unicode] --triplet x64-windows-static
    .\vcpkg install libass boost-flyweight boost-smart-ptr log4cplus[unicode] --triplet x86-windows-static
    .\vcpkg integrate install
    ```
3. Build project xy_sub_filter or vsfilter.
    - If vcpkg is not integrated, pass `/p:VcpkgRoot=<vcpkg root>` to MSBuild.

## Note
This project (XySubFilter with libass) is a **variant** of XySubFilter, which uses [libass](https://github.com/libass/libass) to render **SSA/ASS**, so
1. This project may have some compatibility issues with XySubFilter when rendering SSA/ASS, which is largely dependent on the libass ( of course, we welcome issues, if you don't know whether this comes from libass or from itself )
2. As for the rest of the project ( such as srt, sup and other basic parts ), I basically left it unchanged, so basically it inherits all the strengths and weaknesses of the original project
