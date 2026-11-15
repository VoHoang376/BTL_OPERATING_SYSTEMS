#!/bin/sh
# Generate syscall declarations from syscall.tbl
TBL_FILE=$1
OUT_FILE=$2

echo "/* Auto-generated syscall table */" > "$OUT_FILE"
while read -r num name handler; do
    case "$num" in
        \#*|"") continue ;;
        *) echo "SYSCALL_ENTRY($num, $name, $handler)" >> "$OUT_FILE" ;;
    esac
done < "$TBL_FILE"
