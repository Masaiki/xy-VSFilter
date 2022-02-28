## What is this
This is a subtitle render filter for directshow video players such as mpc-hc/mpc-be, potplayer with madVR, which use libass to render ASS and SSA format subtitles, and origns from https://github.com/Cyberbeing/xy-VSFilter/pull/16, named XySubFilter with libass.

## How to use
1. Run Install_XySubFilter.bat as administrator and dont move or delete the "XySubFilter.dll".
2. Select XySubFilter in your player as your subtitle renderer.
    - Options -> Subtitle -> Subtitle renderer in MPC-BE 
    - Options -> Playback -> Output -> Subtitle Renderer in MPC-HC

## How to compile
1. Clone the repo and run `git submodule update --init --recursive --remote` in the folder
2. In Visual Studio, manully set the Runtime Library of libass and its dependencies to "/MT" for Release and "/MTd" for Debug. See [here](https://docs.microsoft.com/en-us/cpp/build/reference/md-mt-ld-use-run-time-library?view=msvc-170) for more.
3. Build project xy_sub_filter