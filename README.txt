hvinit - Android 11 handoff stub for Galaxy A2 Core (untested on the phone)
1. git init; git add -A; git commit -m hvinit; gh repo create hvinit --public --source=. --push
2. gh run watch   (or: gh run list), then: gh run download -n hvinit
3. python3 repack_a11.py /path/to/boot.img hvinit/hvinit boot_a11.img
4. TWRP > Install > Install Image > boot_a11.img > Boot
Look for "hvinit:" lines in /proc/last_kmsg afterwards.
