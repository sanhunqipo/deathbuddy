This is a desktop "buddy" program created for my needs. It uses Xlib and Cairo.

It has been created using mainly example code from Bernhard R. Fischer, the ones on the Cairo project website showing how to use libcairo in interaction with Xlib. Many Thanks.

Sadly, interfacing with raw X11 calls is particularly difficult. I made an attempt to write without any assitance a version of the program using SDL, but that failed (ex: transparent window would not work on my machine).
Strapped for time and good Xlib resources, AI assistance has been used for the more difficult parts of the code such as dealing with raw key events. When it comes to personal programs, I'd rather use my head, but this was too complex for the amount of time I had.

In any case, the program is straightforward, you may replace the 5 images in the resources folder with your own, but they must have the same filenames and all images must be the same size.
