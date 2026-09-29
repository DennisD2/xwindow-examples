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

### viewer.c
Displays PNG format images from a directory in an endless loop. 
Uses libpng for loading PNG files.

### jviewer.c
Like viewer.c, but can display both PNG and JPEG format images. 
Uses libjpeg for loading JPEG file.

### gviewer.c
Display GIF files, animated and non-animated. Uses gifdec-Implementation
to decode GIF format. Cannot load GIFs without global color table.

## Related
* gifdec code for decoding GIF files - https://github.com/lecram/gifdec.
 
