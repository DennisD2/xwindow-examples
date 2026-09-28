# Small XWindow examples

### pnglogo.c
Loads a PNG file, creates a Pixmap from the loaded data,
and sets the pixmap to be displayed in a PushButton widget.
It uses widget resource XmNpixmap. No scaling of image displayed.
Setup of pixmap is slow.

### pnglogo2.c
Loads a PNG file, creates an XImage from loaded data,
and handles the display of the image inside a Canvas widget.
It uses redisplay/resize callbacks to scale the image if the
root window is manually resized. For scaling, XRender library is used.
Setup and sclaing is very fast.