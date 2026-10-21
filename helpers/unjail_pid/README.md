# CD5 unjail helper

Copyright 2026 Sri7ach1, GPL-3.0-or-later. The author confirmed ownership and
licensed this source on 2026-10-20. Derived from the author's local helper;
original kernel calls retained, process selection corrected to exact path suffix
`/PPSA00001/eboot.bin`. Ambiguous matches and path-query failure are rejected.
Kernel operation failures stop execution; a prior successful operation is not
rolled back. The helper source is supplied, but its ELF is separate from FF PFSC.

Build with `sudo -n ./scripts/build_unjail.sh`. This does not upload or execute it.
The new path selection needs PS5 testing; it intentionally has no UID/PID fallback.

Payload Manager is a launcher, not the source of the helper's kernel functions.
An alternate compatible payload loader may run it after CD5 starts, provided it
passes the SDK payload arguments (kernel read/write socket/pipe descriptors and
kernel addresses). This alternative has not been validated on the user's PS5.
The native application startup has no such payload argument initialization.
Linking these functions into eboot.bin cannot create the missing kernel access.
An independent in-app privilege escalation is outside this change.
