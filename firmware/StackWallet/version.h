#pragma once

// Overwritten by .github/workflows/release.yml before compiling, so that the
// built binary can identify its own GitHub release tag at runtime and the
// OTA client can tell whether it is already up to date. This checked-in
// value is only used for local Arduino IDE builds.
#define STACK_WALLET_VERSION "v0.0.0-dev"
