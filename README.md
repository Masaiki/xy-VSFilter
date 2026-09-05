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

When updating an existing installation, close the player, replace the DLL in
its installed location, and run `Install_XySubFilter.bat` as administrator again
before restarting the player. New property tabs such as `libass` have their own
COM class registrations; replacing or rebuilding the DLL alone leaves those
classes unregistered, so the player can show the old tabs while omitting the new
ones. For a development build without the install script next to it, register
the DLL using `regsvr32.exe "<full path to XySubFilter.dll>"` from an elevated
command prompt.

The **Logs** tab shows a snapshot of recent libass messages from all instances
of this filter DLL in the current player process. Open the tab or click
**Refresh** after reproducing an issue, then use **Copy all** to copy the text.
Logging starts when libass initializes, including for external and embedded
ASS/SSA subtitles. The in-memory buffer retains up to 256 messages (2 KiB per
message, with longer messages marked as truncated); verbose debug output is
excluded. Logs are not saved to disk and disappear when the DLL is unloaded.

## How to compile
1. Clone or download release from https://github.com/ShiftMediaProject/VSYASM and https://github.com/ShiftMediaProject/VSNASM, and run install_script.bat as administrator.
2. Clone the repo and run `git submodule update --init --recursive --remote` in the folder.
3. In Visual Studio, manully set the Runtime Library of libass and its dependencies to "/MT" for Release and "/MTd" for Debug. See [here](https://docs.microsoft.com/en-us/cpp/build/reference/md-mt-ld-use-run-time-library?view=msvc-170) for more.
4. Build project xy_sub_filter.

## VSFilterMod compatibility

The `VSFilter` rendering backend has an explicit `VSFilter compatibility`
setting:

- `xy-VSFilter (default)` preserves the existing xy-VSFilter behavior.
- `VSFilterMod` enables the MOD-only tag semantics.

The setting is disabled, but retained, while another rendering backend is
selected. It is never selected automatically from subtitle contents.

Implemented MOD commands include `\1img` through `\4img`, `\1vc` through
`\4vc`, `\1va` through `\4va`, `\distort`, `\frs`, `\fsvp`, `\jitter`,
`\mover`, `\moves3`, `\moves4`, `\movevc`, `\rnd`, `\rndx`, `\rndy`,
`\rndz`, `\rnds`, and `\z`, together with the VSFilterMod forms of `\fsc`,
`\pos`, and `\org`. External PNG files, ASS `[Graphics]` resources, subtitle
directory lookup, and the Aegisub resource path are supported.

Ordinary solid-color subtitles continue to use the existing SSE2 rasterizer
path. MOD state is allocated only after a MOD feature is applied, PNG files are
decoded through WIC once per resource state, and only gradient/image draw items
use the variable-paint pixel loop. Switching compatibility mode clears rendered
subtitle and bitmap state while retaining the shared lexical ASS-tag cache.

## Note
This project (XySubFilter with libass) is a **variant** of XySubFilter, which uses [libass](https://github.com/libass/libass) to render **SSA/ASS**, so
1. This project may have some compatibility issues with XySubFilter when rendering SSA/ASS, which is largely dependent on the libass ( of course, we welcome issues, if you don't know whether this comes from libass or from itself )
2. As for the rest of the project ( such as srt, sup and other basic parts ), I basically left it unchanged, so basically it inherits all the strengths and weaknesses of the original project

## License

The VSFilterMod-derived compatibility work is covered by GPLv3. See
[NOTICE-VSFilterMod.md](NOTICE-VSFilterMod.md) for source, commit, copyright,
and distribution details.
