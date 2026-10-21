#!/usr/bin/env bash
# Refresh i18n/shady_da.ts from the sources and fill it from i18n/da.py.
set -euo pipefail
cd "$(dirname "$0")/.."
LUPDATE=$(command -v lupdate6 || command -v lupdate || echo /usr/lib/qt6/bin/lupdate)
"$LUPDATE" -silent -no-obsolete src qml -ts i18n/shady_da.ts -target-language da_DK
python3 i18n/fill.py
