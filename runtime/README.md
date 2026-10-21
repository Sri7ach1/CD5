# Native startup runtime

app_crt.cpp is an unmodified copy from PS5SX2:
https://github.com/Swordpdf/PS5SX2/blob/b4cf55e87c34e8a2a1cd9302df4373e5a94d28f4/ps5/proto/native/tooling/native/app_crt.cpp

Copyright (C) 2026 BlackBearReloaded; GPL-3.0-or-later, as stated in its
preserved header. CD5's root LICENSE supplies the GPLv3 text.

Source SHA256:
c0d48a0e1f5036885f784d193cfd3d87e5cfadcbca27ac5e9cea65c7af10f133

Using the inspected SDK image, its native compiler wrapper, clang 18.1.3,
-std=c++20 -O2 -ffunction-sections -fdata-sections produces an object with SHA256:
512f153450aa0bc99143315b68139ec15e9b9e596835609906782fe9b71f611f

That is byte-for-byte identical to the former external runtime/app_crt.o.
The normal build now compiles this source to build/app_crt.o and links that
output. It no longer requires or links the untracked external object.
This evidence resolves the startup object's source correspondence; it was
extended by the completed SDK dependency audit in docs/dependency-audit.md.
