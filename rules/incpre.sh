#!/bin/sh

OS="`uname -s`"
OSVER="`uname -r | cut -f 1 -d -`"
OSVERMAJ="`uname -r | cut -f 1 -d .`"
OSVERMIN="`uname -r | cut -f 2 -d .`"
OSVERSHRT="${OSVERMAJ}.${OSVERMIN}"

cd rules/
rm -f incpre.inc
for file in "${OS}-${OSVER}" "${OS}-${OSVERSHRT}" "${OS}-${OSVERMAJ}" "${OS}" default; do
	if [ -f "${file}" ]; then break; fi
done

echo "include rules/${file}" > incpre.inc
