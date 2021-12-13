#/bin/sh

dd if=/dev/zero of=tb.txt bs=1K count=1

./sim/Vtop &
python3 tb.py

rm tb.txt