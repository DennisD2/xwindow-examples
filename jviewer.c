/********************************************************************
 * This example shows how to load a PNG or a JPEG and use it as a pixmap. Uses libpng, libjpeg and libXrender.
 * The code loads the PNG/JPEG file, creates a XImage definition that is used on redisplay/resize
 * events to set the pixmap in a Canvas widget.
 * Because XRender extension is used for scaling and an XImage structure is used,
 * the rendering is *very* fast.
 * ******************************************************************/

#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <X11/xpm.h>
#include <X11/Intrinsic.h>
#include <X11/extensions/Xrender.h>

#include "png.h"
#include "zlib.h"
#include "stdlib.h"

#include <stdio.h>
#include <jpeglib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <dirent.h>

#define TIMEOUT_MSECONDS 10000L

#define S_NORMAL 0
#define S_PAUSE 1

typedef struct {
    XImage *image;
    int width;
    int height;
    unsigned int depth;
} ImageInfo;

ImageInfo imageInfo;

typedef struct {
    Widget shell;
    Widget canvas;
    XtAppContext app;
    char *dirPath;
    int imageTimeout;
    int state;
    char *filenames[10000];
    int maximage;
    int current;
} AppInfo;

AppInfo appInfo;

static Widget createPixmapCanvas (Widget parent, char *fileName);

int endsWith(const char *str, const char *suffix) {
    size_t len_str = strlen(str);
    size_t len_suffix = strlen(suffix);

    if (len_suffix > len_str) {
        return false;
    }

    return strcmp(str + (len_str - len_suffix), suffix) == 0;
}

static void readpng_version_info() {
    fprintf(stderr, "   Compiled with libpng %s; using libpng %s.\n",
      PNG_LIBPNG_VER_STRING, png_libpng_ver);
    fprintf(stderr, "   Compiled with zlib %s; using zlib %s.\n",
      ZLIB_VERSION, zlib_version);
}

/*
 * Code taken from https://github.com/daneshih1125/xlib/blob/master/xlib_putpng.c
 */
static void teardownPng (png_structp png, png_infop info) {
    if (png) {
        png_infop *realInfo = (info? &info: NULL);
        png_destroy_read_struct (&png, realInfo, NULL);
    }
}

static XImage* loadJPEGToXImage(Display *dpy, Visual *visual, unsigned int depth, const char *filename,
    int *w, int *h) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *infile;
    JSAMPARRAY buffer;
    int row_stride;

    // Open file
    char fn[128];
    sprintf(fn,"%s/%s", appInfo.dirPath, filename);
    printf("Open file %d: %s\n", appInfo.current, fn);
    if ((infile = fopen(fn, "rb")) == NULL) {
        fprintf(stderr, "Error opening %sn", filename);
        return NULL;
    }

    // init decompression
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, infile);

    // read header and start decompression
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    int width = cinfo.output_width;
    int height = cinfo.output_height;
    *w = width;
    *h = height;
    int components = cinfo.output_components; // Meistens 3 (RGB)

    // 4. Ein leeres XImage mit den korrekten Dimensionen erstellen
    // Wir lassen Xlib den Speicher für die Pixeldaten selbst allozieren (data = NULL, bytes_per_line = 0)
    XImage *ximage = XCreateImage(dpy, visual, depth, ZPixmap, 0, NULL, width, height, 32, 0);
    if (!ximage) {
        fprintf(stderr, "Fehler: Konnte XImage nicht erstellen\n");
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        fclose(infile);
        return NULL;
    }
    // Alloc memory for image pixels
    ximage->data = malloc(ximage->bytes_per_line * height);

    // Prepare line buffer
    row_stride = width * components;
    buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

    // decompress, line-by-line, and write to XImage
    int y = 0;
    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, buffer, 1);

        // Color layout libjpeg is RGB.
        // X11 erwartet auf modernen Linux-Systemen (TrueColor 24/32 Bit) meist BGR.
        for (int x = 0; x < width; x++) {
            unsigned char r = buffer[0][x * components + 0];
            unsigned char g = buffer[0][x * components + 1];
            unsigned char b = buffer[0][x * components + 2];

            // create pixelvalue
            // Für ein typisches 24/32-Bit TrueColor Visual auf Linux:
            unsigned long pixel = (r << 16) | (g << 8) | b;

            XPutPixel(ximage, x, y, pixel);
        }
        y++;
    }

    // Cleanup
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(infile);

    return ximage;
}

/*
 * Code taken from https://github.com/daneshih1125/xlib/blob/master/xlib_putpng.c
 */
static void loadPng(FILE *file, unsigned char** data, char **clipData, unsigned int *width, unsigned int *height,
    unsigned int *rowbytes) {
    size_t size = 0, clipSize = 0;

    png_structp png = NULL;
    png_infop info = NULL;
    unsigned char **rowPointers = NULL;

    int depth = 0,
    colortype = 0,
    interlace = 0,
    compression = 0,
    filter = 0;
    unsigned clipRowbytes = 0;

    png = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png_create_info_struct (png);
    png_init_io (png, file);
    png_read_info (png, info);
    png_get_IHDR (png, info, (png_uint_32*)width, (png_uint_32*)height, &depth, &colortype, &interlace, &compression, &filter);

    *rowbytes = png_get_rowbytes (png, info);

    if (colortype == PNG_COLOR_TYPE_RGB) {
        // X hates 24bit images - pad to RGBA
        png_set_filler (png, 0xff, PNG_FILLER_AFTER);
        *rowbytes = (*rowbytes * 4) / 3;
    }

    png_set_bgr (png);
    *width = png_get_image_width (png, info);
    *height = png_get_image_height (png, info);
    size = *height * *rowbytes;

    clipRowbytes = *rowbytes/32;
    if (*rowbytes % 32)
        ++clipRowbytes;
    clipSize = clipRowbytes * *height;
    // This gets freed by XDestroyImage
    *data = (unsigned char*) malloc (sizeof (png_byte) * size);

    rowPointers = (unsigned char**) malloc (*height * sizeof (unsigned char*));

    png_bytep cursor = *data;

    int i=0,x=0,y=0;

    for (i=0; i<*height; ++i, cursor += *rowbytes)
        rowPointers[i] = cursor;

    png_read_image (png, rowPointers);
    *clipData = (char*) calloc (clipSize, sizeof(unsigned char));

    if (colortype == PNG_COLOR_TYPE_RGB) {
        memset (*clipData, 0xff, clipSize);
    } else {
        // Set up bitmask for clipping fully transparent areas
        for (y=0; y<*height; ++y, cursor+=*rowbytes) {

            for (x=0; x<*rowbytes; x+=4) {
                // Set bit in mask when alpha channel is nonzero
                if(rowPointers[y][x+3])
                    (*clipData)[(y*clipRowbytes) + (x/32)] |= (1 << ((x/4)%8));
            }
        }
    }
    teardownPng (png, info);
    free (rowPointers);
}

/**
 * Handles geometry changes. uses XRender extension for fast scaling.
 * @param canvas widget with geometry changed
 */
static void handleGeometryChanges(Widget canvas) {
    Display *dpy = XtDisplay(canvas);
    Window win = XtWindow(canvas);
    XImage *src_ximage = imageInfo.image;

    // Get new geometry
    XtWidgetGeometry intended,prefered;
    XtQueryGeometry(XtParent(canvas), &intended, &prefered);
    Dimension new_w = prefered.width;
    Dimension new_h = prefered.height;
    //printf("new_w=%d, new_h=%d\n", new_w, new_h);

    // Prevent X errors as long as there is no physical window mapped
    if (win == 0) {
        printf("waiting for window becoming mapped...\n");
        return;
    }

    if (src_ximage == NULL) {
        printf("no ximage was created, ignoring...\n");
        return;
    }

    Pixmap temp_pixmap = XCreatePixmap(dpy, win,
                                       src_ximage->width, src_ximage->height,
                                       src_ximage->depth);

    // copy ximage into temp pixmap
    GC gc = XCreateGC(dpy, temp_pixmap, 0, NULL);
    XPutImage(dpy, temp_pixmap, gc, src_ximage, 0, 0, 0, 0,
              src_ximage->width, src_ximage->height);
    XFreeGC(dpy, gc);

    // create XRender-"Pictures" from source pixmap and for destination
    XRenderPictureAttributes pa;
    XRenderPictFormat *fmt = XRenderFindVisualFormat(dpy, DefaultVisual(dpy, 0));

    Picture src_pic = XRenderCreatePicture(dpy, temp_pixmap, fmt, 0, &pa);
    Picture dest_pic = XRenderCreatePicture(dpy, win, fmt, 0, &pa);

    // bilinear filter for scaling
    XRenderSetPictureFilter(dpy, src_pic, FilterBilinear, NULL, 0);

    // calculate transforming matrix
    XTransform xform = {{
        { XDoubleToFixed((double)src_ximage->width / new_w), 0, 0 },
        { 0, XDoubleToFixed((double)src_ximage->height / new_h), 0 },
        { 0, 0, XDoubleToFixed(1.0) }
    }};
    XRenderSetPictureTransform(dpy, src_pic, &xform);

    // Render scaled image into canvas window
    XRenderComposite(dpy, PictOpSrc, src_pic, None, dest_pic,
                     0, 0, 0, 0, 0, 0, new_w, new_h);

    // cleanup
    XRenderFreePicture(dpy, src_pic);
    XRenderFreePicture(dpy, dest_pic);
    XFreePixmap(dpy, temp_pixmap); // Die temporäre Pixmap kann wieder weg
}

static void exposeCallback(Widget canvas, XtPointer xt_pointer, XtPointer xt_pointer1) {
    //printf("exposeCallback()\n");
    handleGeometryChanges(canvas);
    XtVaSetValues(XtParent(canvas), XmNwidth, imageInfo.width, XmNheight, imageInfo.height, NULL);
}

static void resizeCallback(Widget canvas, XtPointer xt_pointer, XtPointer xt_pointer1) {
    //printf("resizeCallback()\n");
    handleGeometryChanges(canvas);
}

static bool loadPngFromFile(char *pngFile, unsigned char **data, int *png_bytes) {
    char *clip = NULL;
    // Open PNG file
    char fn[128];
    sprintf(fn,"%s/%s", appInfo.dirPath, pngFile);
    printf("Open file %d: %s\n", appInfo.current, fn);
    FILE *fp = fopen(fn, "rb");
    if (!fp) {
        fprintf(stderr, "Error opening file\n");
        return false;
    }
    loadPng(fp, data, &clip, &imageInfo.width, &imageInfo.height, png_bytes);
    fclose(fp);
    return true;
}

static XImage *createImageFromFile(Widget parent, char *pngFile, unsigned char **data, int *png_bytes) {
    if (loadPngFromFile(pngFile, data, png_bytes)==false) return false;

    Display *dpy = XtDisplay(parent);
    int screen = DefaultScreen(dpy);
    XImage *image = XCreateImage (dpy, DefaultVisual(dpy, screen),
        DefaultDepth(dpy, screen), ZPixmap, 0, (char*)*data, imageInfo.width, imageInfo.height, 32, *png_bytes);
    return image;
}

static Widget createPixmapCanvas(Widget parent, char *fileName) {
    Widget canvas;
    Display *dpy = XtDisplay(parent);

    canvas = XtCreateManagedWidget ( "canvas", xmDrawingAreaWidgetClass, parent, NULL, 0 );
    appInfo.canvas = canvas;
    XtAddCallback ( canvas, XmNexposeCallback, exposeCallback,  ( XtPointer )NULL );
    XtAddCallback ( canvas, XmNresizeCallback, resizeCallback, ( XtPointer )NULL );


    int screen = DefaultScreen(dpy);
    imageInfo.depth = DefaultDepth(dpy, screen);

    if (endsWith(fileName, ".png")) {
        unsigned char *data;
        int png_bytes;
        imageInfo.image = createImageFromFile(appInfo.shell, fileName, &data, &png_bytes);
    }
    if (endsWith(fileName, ".jpg")) {
        Visual *v = DefaultVisual(dpy, DefaultScreen(dpy));
        imageInfo.image = loadJPEGToXImage(dpy, v, imageInfo.depth, fileName,
            &(imageInfo.width), &(imageInfo.height));
    }
    return canvas;
}

/*
 * Timeout callback
 */
static void TimeoutCB( XtPointer client_data, XtIntervalId* id ) {
    //printf("TimeoutCB\n");

    if (appInfo.state == S_PAUSE) {
        XtAppAddTimeOut( appInfo.app, appInfo.imageTimeout, TimeoutCB, NULL );
        return;
    }

    char *file = appInfo.filenames[appInfo.current];
    appInfo.current++;
    if (appInfo.current==appInfo.maximage) {
        appInfo.current=0;
    }

    if (imageInfo.image != NULL) {
        XDestroyImage(imageInfo.image);
    }

    if (endsWith(file, ".png")) {
        int png_bytes;
        unsigned char *data;
        imageInfo.image = createImageFromFile(appInfo.shell, file, &data, &png_bytes);
    }
    if (endsWith(file, ".jpg")) {
        Display *dpy = XtDisplay(appInfo.shell);
        Visual *v = DefaultVisual(dpy, DefaultScreen(dpy));
        imageInfo.image = loadJPEGToXImage(dpy, v, imageInfo.depth, file,
            &imageInfo.width, &imageInfo.height);
    }
    XtVaSetValues(XtParent(appInfo.canvas), XmNwidth, imageInfo.width, XmNheight, imageInfo.height, NULL);
    handleGeometryChanges(appInfo.canvas);
    /*
     * start time out from the beginning
     */
    XtAppAddTimeOut( appInfo.app, appInfo.imageTimeout, TimeoutCB, NULL );
}

static void canvasKeyEventHandler(Widget widget, XtPointer clientData, XEvent * event, Boolean * flag) {
    //printf("canvasKeyEventHandler\n");
    XKeyEvent *ke = (XKeyEvent *) event;
    char buffer[16];
    KeySym keysym;
    int num_chars = XLookupString(&event->xkey, buffer, sizeof(buffer) - 1, &keysym, NULL);
    if (num_chars > 0) {
        buffer[num_chars] = '\0';
        char ks = buffer[0];
        if (ks==' ') {
            if (appInfo.state == S_NORMAL) {
                appInfo.state = S_PAUSE;
                printf("paused\n");
            } else {
                appInfo.state = S_NORMAL;
                printf("normal\n");
            }
        }
        if (ks=='q' || ks=='x') {
            exit(0);
        }
    } else {
        if (keysym == XK_Left) {
            appInfo.current--;
            if (appInfo.current<0) {
                appInfo.current=appInfo.maximage-1;
            }
            printf("current=%d\n", appInfo.current);
        }
        if (keysym == XK_Right) {
            appInfo.current++;
            if (appInfo.current==appInfo.maximage) {
                appInfo.current=0;
            }
            printf("current=%d\n", appInfo.current);
        }
    }
}

void main( int argc, char **argv ) {
    Widget canvas, shell;
    XtAppContext app;

    char *baseDir = ".";
    if (argc >= 2) {
        baseDir = argv[1];
    }

    int startIndex = 0;
    if (argc == 3) {
        startIndex = atoi(argv[2]);
    }
    printf("basedir=%s, startIndex=%d\n", baseDir, startIndex);

    readpng_version_info();

    shell = XtAppInitialize ( &app, "XPmlogo", NULL, 0,
                              &argc, argv, NULL, NULL, 0  );
    appInfo.app = app;
    appInfo.shell = shell;
    appInfo.imageTimeout = TIMEOUT_MSECONDS;
    appInfo.maximage = 0;

    DIR *dir;
    struct dirent *entry;
    appInfo.dirPath = baseDir;
    dir = opendir( appInfo.dirPath);
    if (dir == NULL) {
        perror("Fehler beim Öffnen des Verzeichnisses");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        char  *name = entry->d_name;
        if (endsWith(name, ".png") || endsWith(name, ".jpg")) {
            //printf("- %s\n", entry->d_name);
            appInfo.filenames[appInfo.maximage] = malloc(strlen(name)+1);
            strcpy(appInfo.filenames[appInfo.maximage], name);
            appInfo.maximage++;
        }
    }
    closedir(dir);

    if (startIndex >= appInfo.maximage) {
        printf("Start index %d larger than number of images %d, ignoring.\n", startIndex, appInfo.maximage);
    }
    appInfo.current = startIndex;

    canvas = createPixmapCanvas( shell,  appInfo.filenames[0] );
    XtAddEventHandler ( canvas, KeyPressMask, FALSE,
            canvasKeyEventHandler, NULL );

    printf("Number of files: %d\n", appInfo.maximage);

    XtRealizeWidget ( shell );

    // Kick off timeout
    TimeoutCB( NULL, NULL );

    XtAppMainLoop ( app );
}