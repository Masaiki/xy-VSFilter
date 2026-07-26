# libass regression fixture

`libass_regression.ass` is an original, synthetic subtitle written for the
xy-VSFilter regression suite. It does not contain dialogue, styles, drawings,
or effect code copied from an external subtitle.

The test loads `SMP/libass/compare/test/font1.ttf` privately and disables
system font providers. The font identifies itself as **Pixel Operator Mono**
and contains the following license metadata:

> Released by Jayvee Enaguas (HarvettFox96), licensed under Creative Commons
> Zero (CC0) 1.0.

Source: the libass `compare/test` assets pinned by this repository's libass
submodule.

`libass_regression.golden.tsv` is generated explicitly by
`scripts/Run-RegressionTests.ps1 -UpdateGolden -GoldenBaselineSha <sha>`.
Normal test and CI runs never rewrite it.
