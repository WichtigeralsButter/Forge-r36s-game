#!/bin/bash
# Forge - Startskript fuer ArkOS (R36S)
cd "$(dirname "$(readlink -f "$0")")/forge" || exit 1
chmod +x ./forge
printf "\033c" > /dev/tty1
./forge > log.txt 2>&1
printf "\033c" > /dev/tty1
