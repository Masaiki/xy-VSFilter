# Native Pixel Regression

`runner.cpp` renders through `CRenderedTextSubtitle` with the VSFilter backend.
It records the render HRESULT, SHA-256 of the entire output buffer, and whether
the frame differs from the initial background. Repeated and backwards timestamps
exercise populated caches. An optional CSRI DLL provides a reference renderer.

Build `runner.vcxproj` with MSBuild, Release/x64, and an absolute `SolutionDir`
ending in a backslash. It reuses the unit-test project's includes and libraries;
build the solution libraries first. The executable is placed alongside
`unit_test.exe`. Baseline and candidate must have independent library/output
directories and use the same compiler, fonts, and test inputs.

```powershell
python test/pixel_regression/regression.py generate --subtitle 'D:\path\sample.ass'
python test/pixel_regression/regression.py run --runner 'D:\baseline\pixel_runner.exe' --after bin/pixel-regression/before.tsv
python test/pixel_regression/regression.py run --runner 'D:\candidate\pixel_runner.exe' --after bin/pixel-regression/after.tsv
python test/pixel_regression/regression.py compare --before bin/pixel-regression/before.tsv --after bin/pixel-regression/after.tsv
```

Generation requires Pillow; orchestration and comparison use the Python standard
library. Generated inputs, manifests, logs, and results live under ignored `bin/`.
The original subtitle and font attachments are not copied into the source tree.

`resume` preserves completed jobs and runs incomplete jobs in separate processes.
Use `--workers N` to limit concurrency and `--timeout SECONDS` for each job.
Each job retains its complete timestamp sequence in one native renderer instance.
Failures are written to `<result>.failures.json`; missing frames are separate from
pixel mismatches in `comparison.json`. A successful Python process does not mean
all render cases passed: inspect both files. `run` uses a 20-minute timeout per
100-job batch and is intended for smaller suites.

`--font-dir DIRECTORY` loads that directory's `.ttf` files privately in the native
process, only for the single `user/` subtitle source in the manifest. Extract
attachments from the original media before testing. Do not resume completed jobs
after changing their inputs, fonts, renderer, or configuration; start new results.

The generated matrix contains 45 synthetic fixtures, all `test/*.ass` files, and
the optional real subtitle. Synthetic cases span both compatibility modes, all
five subpixel levels, RGB32/RGBA/planar AYUV, and two output sizes. Corpus files
use both modes and all three surfaces at 640x360. The real subtitle uses 1920x1080,
both modes, exact/interpolated eighth-pixel positioning, and all three surfaces.
Corpus and real-file sampling covers each valid event's start, midpoint, final
centisecond, and end, deduplicated across events. This is event-based sampling,
not every video frame or every possible animation state.

For CSRI comparison, create a manifest containing MOD/exact-eighth-pixel/RGB32
jobs, then pass `--reference PATH_TO_DLL` to `run` or `resume`. Pass the same option
to `compare` to ignore HRESULT differences because CSRI render returns no status.
Only expected job/frame keys are compared. Reference DLL configuration and build
provenance must be recorded separately; equal output against a refactor baseline
does not establish equality with the reference renderer.

Native TSV input rows contain four tab-separated fields:
`id`, UTF-8 absolute subtitle path, `width height mode level surface background`,
and space-separated timestamps in seconds. Mode is 0/1; level is 0..4; surface is
0=RGB32, 1=RGBA, 2=planar AYUV; background is 0=black or 1=colored.
Optional native argument 3 dumps raw buffers to an existing directory; use `-`
to skip dumping when supplying a CSRI DLL as argument 4. RGB dumps are BGRA byte
order, while AYUV dumps contain four contiguous planes.
