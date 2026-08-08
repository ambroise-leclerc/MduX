# `dejavu-ui` font source

`DejaVuSans.ttf` is DejaVu Sans, redistributed **unmodified** as a build-time input to
[`recipes/font/dejavu-ui.toml`](../dejavu-ui.toml). `mdux-textbake` reads it and rasterises
glyphs from it; nothing links it, and no build step rewrites it.

| | |
|---|---|
| Upstream | https://dejavu-fonts.github.io/ |
| License text | [`LICENSE-DejaVu.txt`](LICENSE-DejaVu.txt) |
| License source | https://github.com/dejavu-fonts/dejavu-fonts (`LICENSE`) |
| SOUP entry | [`docs/governance/soup-register.toml`](../../../docs/governance/soup-register.toml) |

## Why the license file has nothing of ours in it

`LICENSE-DejaVu.txt` is the upstream file, byte for byte, with no header, note or edit of ours -
which is why this README exists to carry the provenance instead.

Two earlier attempts got that wrong and are worth recording, because both looked tidier than they
were:

1. It first held only the Bitstream Vera stanza extracted from Debian's machine-readable
   `copyright` file. That file contains **no Arev notice at all**, so the result claimed "with the
   Arev additions" while omitting the Tavmjong Bah copyright and the Arev terms that DejaVu
   actually incorporates. Both licenses require their own notice to accompany redistributed
   copies, so an extraction that drops one is a licensing defect however clean it reads.
2. It then held the upstream text with a repository-authored header above it, described as
   "verbatim". The license text was; the file was not.

The rule that avoids both: a license file contains the license and nothing else.

## The naming restrictions

The Bitstream and Arev licenses both restrict use of their names in *modified* versions. MduX does
not modify the font, so neither is engaged. A future change that subsets or edits the `.ttf`
before committing it would engage them, and would need the committed file renamed.

The coverage bitmaps baked into `generated/font/dejavu-ui/atlas.bin` are output of *using* the
font, the same as printing a document with it - not a modified version of the font software.
