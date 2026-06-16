# smice

smice (_Smooth Mice_) is CLI tool for Linux that stabilizes cursor movements by intercepting raw hardware events and applying a smoothing algorithm.

## Getting Started

Because smice communicates directly with the Linux kernel's input subsystem (`/proc/bus/input/devices`, `/dev/input/` and `/dev/uinput`), it does not require any external libraries. However, it does require root privileges to run so it can safely grab and clone your input devices.


To compile the project, simply run
```
make
```

(Alternatively, compile it directly with: `gcc -O2 smice.c -o smice`)

To start smoothing your cursor with the default settings (triggers smoothing while holding down the left click):
```
sudo ./smice
```

To exit the program safely and restore your hardware controls, simply press `CTRL+C`.

smice offers additional optionas to tune the smoothing behavior:
```
    -s FACTOR, --smooth-rate=FACTOR
        Sets the intensity of the smoothing effect

    -a ACTION_LIST, --actions=ACTION_LIST
        A comma-separated list of triggers that engage the smoothing effect
        Available actions are ALWAYS, LEFT-CLICK, RIGHT-CLICK, MIDDLE-CLICK, and ON-TOUCH
```

For more details see `./smice --help`.

_Now it's your turn! Launch the program and try drawing a perfect circle! (Be careful, it will take some practice.)_
