# VSFilterMod compatibility notice

The VSFilterMod compatibility implementation in this repository is based on
the behavior and rendering algorithms of
[Masaiki/VSFilterMod](https://github.com/Masaiki/VSFilterMod), inspected at
commit `8556b1bc362fc46977eeb86ee9d9f2a7d1312399`.

The original VSFilterMod work is licensed under the GNU General Public License,
version 3. Copyright in that work remains with its contributors, including the
authors recorded in its Git history. Existing Gabest and xy-VSFilter copyright
notices are retained in the inherited source files.

The new compatibility implementation covers tag semantics, transforms,
gradients, image paint, resource lookup, motion, jitter, and related cache
behavior. Files and code paths containing VSFilterMod-derived material must be
distributed under GPLv3-compatible terms. A complete copy of GPLv3 is already
included in this source tree at `SMP/liblzma/COPYING.GPLv3`.

`License.txt` remains the license notice for the original xy-VSFilter code.
Distributors must comply with both that notice and the GPLv3 requirements
applicable to the VSFilterMod-derived combined work.
