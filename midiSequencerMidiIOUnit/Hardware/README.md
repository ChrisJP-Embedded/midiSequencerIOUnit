# This is the Kicad 6 project for the 6 x 8 sequencer input unit.

**Summary**

NOTE: This is an initial prototype. 

The IO unit provides a wireless interface to the physical MIDI layer.
The midi physical layer is essentially an opto-isolated 31.25K buad uart which runs at 5V.

An ESP32S3 MCU controls the unit. USB port provides programming, console, and JTAG interface.
USB traces designed for 90ohm differential impedance.


![Model](https://github.com/ChrisJP-Embedded/midiSequencer/blob/main/images/IOUnitPCB.png)
