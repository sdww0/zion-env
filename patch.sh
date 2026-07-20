#! /bin/bash

set -euo pipefail

cat <<'MSG'
patch.sh is deprecated: no local patch application is required.

Zion component changes are maintained on their public Git branches. Run:

  ./download.sh

Then build the stack separately with:

  ./build-all.sh
MSG
