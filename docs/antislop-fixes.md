# Antislop findings addressed, 2026-10-22

The four findings from the static review are addressed without changing playback
or recovery behavior. The existing purple palette, fonts and three-column layout
are retained.

- R-25 PASS for the changed secondary text: #9a8fa3 measures 6.39:1 on
  #0d0a13, 5.98:1 on #17121f and 5.38:1 on #251a31 using the antislop contrast
  checker. This is a check of these pairs, not every color in the application.
- Action wording PASS: the permanent OPTIONS hint says it reloads the disc and
  plays from track 1. Error messages no longer imply an isolated network retry.
  The README describes the same action and distinguishes published 0.9.0-beta
  from candidate 0.9.1-beta.
- Layout measurement PASS: the new hint measures 208.58 pixels with the supplied
  Ubuntu font and stb_truetype at height 13, within its 390-pixel allocation.
  It starts at y=908, above the credits at y=971.
- Comment review PASS for the reported scope: metadata state documentation now
  matches the assignments and sits beside its declaration. Decorative separator
  banners are removed; protocol, concurrency and license notes are preserved.
- Build and regression checks PASS: all ten test groups and the native build
  completed successfully; git diff --check is clean.

New eboot SHA256:
`92cf72a301facd408f7a13c23ace9cda205c78110afea1ca760f4efd692f516d`.

The older audited package predates these UI changes. Use the package named for
this correction's commit and its paired corresponding-source archive. No PS5
visual or controller walkthrough was performed, so full visual approval remains
pending. No new style direction, assets, or global antislop settings were added.
