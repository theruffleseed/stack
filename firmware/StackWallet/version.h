#pragma once

// Overwritten by .github/workflows/release.yml before compiling, so that the
// built binary can identify its own GitHub release tag at runtime and the
// OTA client can tell whether it is already up to date. This checked-in
// value is only used for local Arduino IDE builds.
//
// When a GitHub release is built from the current main, stamp this with
// that release's tag (e.g. "v0.1.3") so local builds identify themselves
// correctly and don't try to OTA-reflash the same version back onto the
// device. Bump it whenever you pull new code from main.
#define STACK_WALLET_VERSION "v0.1.14"
