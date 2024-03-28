#!/bin/bash
file=sender.c
rm -f OutputFile
pkill sender
pkill receiver
./waitForPorts
./receiver delaysyn.script & sleep 1
./sender   $file
sleep 2
pkill receiver
diff $file OutputFile
