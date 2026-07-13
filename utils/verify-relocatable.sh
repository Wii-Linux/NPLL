#!/bin/sh
set -eu

elf=$1
readelf=$2
nm=$3

sections=$($readelf -S "$elf")
relocs=$($readelf -r "$elf")
dynamic=$($readelf -d "$elf")

printf '%s\n' "$sections" | grep -q '\.got2' || {
	echo "verify-relocatable: missing .got2" >&2
	exit 1
}
printf '%s\n' "$sections" | grep -q '\.fixup' || {
	echo "verify-relocatable: missing .fixup" >&2
	exit 1
}
printf '%s\n' "$relocs" | grep -q 'There are no relocations' || {
	echo "verify-relocatable: final ELF contains unresolved relocations" >&2
	exit 1
}
printf '%s\n' "$dynamic" | grep -q 'There is no dynamic section' || {
	echo "verify-relocatable: final ELF requires dynamic linking" >&2
	exit 1
}

runtime_start=$($nm "$elf" | awk '$3 == "__reloc_dest_start" { print "0x" $1 }')
stack_top=$($nm "$elf" | awk '$3 == "__stack_top" { print "0x" $1 }')
[ -n "$runtime_start" ] && [ -n "$stack_top" ] || {
	echo "verify-relocatable: missing runtime boundary symbols" >&2
	exit 1
}
[ $((stack_top - runtime_start)) -le $((0x100000)) ] || {
	echo "verify-relocatable: runtime exceeds the 1 MiB Wii U reservation" >&2
	exit 1
}

undefined=$($nm -u "$elf" || true)
[ -z "$undefined" ] || {
	echo "verify-relocatable: undefined symbols remain:" >&2
	printf '%s\n' "$undefined" >&2
	exit 1
}
