#!/bin/sh
# SPDX-License-Identifier: Unlicense
#
# One test, one X server. Meson may run these together. Each server has its
# own clipboard, so a cut or a selection in one test cannot land in another.
# xvfb-run chooses a free display number.

exec xvfb-run -a "$@"
