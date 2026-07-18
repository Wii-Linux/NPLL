#!/bin/sh
#
# NPLL - Utilities - DOL virtual fixup
#
# Copyright (C) 2026 Techflash
#

if ! [ -f "$1" ]; then
	echo "Missing input file"
	exit 1
fi

TOOLS=external/dol-tools/bin
if ! [ -e "$TOOLS/dol-info" ] || ! [ -e "$TOOLS/dol-patch" ]; then
	echo "Missing dol-info/dol-patch"
	exit 1
fi

cp "$1" "$2"

"$TOOLS/dol-info" "$1" | awk '
	/^  Text sections:$/ { section = "text"; next }
	/^  Data sections:$/ { section = "data"; next }
	section != "" && $1 ~ /^[0-9]+:$/ {
		sub(/:$/, "", $1)
		sub(/,$/, "", $3)
		sub(/,$/, "", $5)
		if ($5 != "0x0")
			print section $1 "_addr", $3
	}
	/^BSS Address:/ { bss = $3 }
	/^BSS Size:/ && $3 != "0x00000000" { print "bss_addr", bss }
	/^Entry point:/ { print "entry", $3 }
' | while read -r FIELD ADDRESS; do
	VIRTUAL=$(printf '%08x' "$((ADDRESS | 0x80000000))")
	"$TOOLS/dol-patch" "$2" "$FIELD" "$VIRTUAL"
done
