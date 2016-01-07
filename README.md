Unified Position Provider Daemon
================================

This is unified position provider for Linux (besides from
the Bluetooth features should work on any unixy OS with little
or no modification). It can take the position from different 
providers (currently GPS and Wifi position) and provides the most 
accurate location to other programs. Providers are implemented as
plugins. It is in very early stages of development but it's working.

What works
----------

- Wifi position provider based on Skyhook WPS API library
- GPS Provider using NMEA standard.

What doesn't work yet
---------------------

- There's no algorithm for selecting the best position. GPS
  is preferred over Wifi.

- Only bluetooth GPS is working (tested using Android's GPS over 
  BT app), but 99% of the code will be shared between the BT and
	USB or serial GPS. Only the code to open the serial port needs
	to be written and tested, so it'll be working soon.

- There's a race condition when another program tries to scan for
  bluetooth devices and wifi access point while uppd is scanning.
	It causes NetworkManager to log an error on occassion but
	connectivity is not lost. More research is needed to fix this
	issue.

- The build system needs much improvement. Adding options to 
  disable providers, etc.

- The Wifi provider works flawlessly but it requires a proprietary
  library from Skyhook Wireless. The plan is to write a provider
	that uses MLS (Mozilla Location Service), the provider used
	by Firefox.

- The commit logs suck. Once we reach a v0.1 release I will rebase
  them on a new repo called uppd.
