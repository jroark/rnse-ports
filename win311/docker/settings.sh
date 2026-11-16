#!/bin/sh
# The settings win311/install.py makes in a fresh DOS and Windows for the
# 192, run in the container of win311/docker with mtools' drive c: the
# image (or, given a name, /i/NAME):   settings.sh [IMAGE.IMG]
# WIN at the end of AUTOEXEC.BAT: straight to Windows.  HIMEM.SYS without
# its test of extended memory and SWITCHES=/F, no two seconds' pause:
# the start of DOS at the emulated PC's pace.  Paging=No in SYSTEM.INI:
# no swap file (what the PC writes stays in the unit's RAM).
set -e
export MTOOLS_SKIP_CHECK=1
[ -z "$1" ] || echo "drive c: file=\"/i/$1\" partition=1" > /root/.mtoolsrc
cd /tmp
mcopy -n c:/AUTOEXEC.BAT a.bat
mcopy -n c:/CONFIG.SYS c.sys
mcopy -n c:/WINDOWS/SYSTEM.INI s.ini
cr=$(printf '\r')
grep -q "^WIN$cr\$" a.bat || printf 'WIN\r\n' >> a.bat
sed -i "s|^\(DEVICE=C:\\\\DOS\\\\HIMEM.SYS\)$cr\$|\1 /TESTMEM:OFF$cr|" c.sys
grep -q '^SWITCHES' c.sys || printf 'SWITCHES=/F\r\n' >> c.sys
grep -q '^Paging=' s.ini || sed -i "s/^\[386Enh\]$cr\$/[386Enh]$cr\nPaging=No$cr/" s.ini
mcopy -o a.bat c:/AUTOEXEC.BAT
mcopy -o c.sys c:/CONFIG.SYS
mcopy -o s.ini c:/WINDOWS/SYSTEM.INI
mtype c:/CONFIG.SYS | tr -d "\r"; mtype c:/AUTOEXEC.BAT | tr -d "\r"
