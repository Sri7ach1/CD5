# Building and installing the 0.9.1-beta candidate

This is an untested-on-hardware candidate. Review dependency-audit.md before
public distribution. Local packaging is distinct from publishing a release.

## Build environment

Use an Ubuntu host with Docker and the existing PS5 Payload SDK image. The
inspected image ID and SDK source commit are recorded in sdk-inventory.json and
dependency-audit.md. The native PS5SX2 tooling is external and must be provided.
Set CD5_NATIVE_SDK to its `ps5/proto/native` directory. CD5_SDK_IMAGE optionally
selects the SDK image (default: ps5-payload-sdk). The build requires Clang 18,
ld.lld-18, the native wrapper, ps5-pie.ld, app-symbols.map and ps5-native-tool.

The startup source is now supplied in runtime/app_crt.cpp and built on every
invocation. No prebuilt runtime/app_crt.o is needed. No additional linked
library has been introduced for the 0.9.1 changes.

From the repository root, with CD5_NATIVE_SDK set in your environment:

```bash
./scripts/test.sh --sdk
./scripts/build.sh
CD5_PACKAGE_OUTPUT="$PWD/dist/CD5-0.9.1-beta-candidate.ffpfsc" ./scripts/package_ffpfsc.sh
```

If your host requires sudo, pass the native directory explicitly with `sudo env
CD5_NATIVE_SDK=...` using your configured value. Root builds can leave generated
files owned by root; use a consistent build account. Do not relax global Docker
socket permissions to work around that.

Build output is package/PPSA00001/eboot.bin. The packaging script verifies the
successful build manifest and refuses to overwrite an existing output. Choose
a fresh candidate filename after another build. The historical dist/CD5.ffpfsc
is not the new candidate and is preserved.

scripts/build_and_push.sh is a legacy name: it builds and deploys to the console.
It does not perform Git push. Use build.sh for build-only verification.

## Console prerequisites and order

The only previously verified setup is PS5 Slim, firmware 13.60, Relapse and
LiteON eBAU102-21. This candidate has not yet been tested there.

1. Obtain Payload Manager separately and build the included GPL helper with
   `sudo -n ./scripts/build_unjail.sh`. Its source is in helpers/unjail_pid; the
   separate build/unjail_pid.elf is not embedded in the FF PFSC. The corrected
   path selector still requires PS5 validation.
2. Install the helper at /data/pldmgr/payloads/unjail_pid/unjail_pid.elf and have
   Payload Manager listening locally on port 8084 before starting CD5.
3. Install the candidate using your existing application installation workflow.
4. CD5 initializes networking, requests the helper over loopback, then searches
   optical CAM devices. HTTP success alone does not prove privilege elevation.
5. Connect the optical drive and insert an Audio CD. Mixed CDs need a supported
   raw TOC command. Use OPTIONS to retry disc information/network work.

The optional developer launcher requires PS5_IP and CD5_UNJAIL_ELF explicitly.
It closes an existing instance, launches the application through the shell,
uploads the supplied autonomous helper and requests execution. It no longer
edits TARGET_PID or rebuilds an external source file. It requires the console
shell (PS5_SHELL_PORT, default 2323), FTP 2121 and Payload Manager 8084.

scripts/deploy.sh overwrites the installed eboot through console FTP. Its
PS5_IP is mandatory. These scripts are never run by the build or test scripts;
console operations require explicit maintainer authorization.

For source/license delivery, see source-delivery.md. Payload Manager can in
principle be replaced by a compatible SDK payload loader running the helper after
CD5 starts. The automatic loopback request then fails harmlessly and discovery
continues. This alternate loader path is not hardware-validated. Embedding kernel
calls in the native application alone cannot create the required kernel access.
