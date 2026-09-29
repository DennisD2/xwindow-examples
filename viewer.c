/********************************************************************
 * This example shows how to load a PNG and use it as a pixmap. Uses libpng and libXrender.
 * The code loads the PNG file, creates a XImage definition that is used on redisplay/resize
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

#include <dirent.h>

#define TIMEOUT_NOSECONDS 10000L

typedef struct {
    XImage *image;
    int width;
    int height;
    GC gc;
    unsigned int depth;
    Widget shell;
    Widget canvas;
    XtAppContext app;
    char *dirPath;
} ImageInfo;

ImageInfo imageInfo;

char *filenames[10000];
int maximage = 0;
int current = 0;

Widget createPixmapCanvas (Widget parent, char *fileName);

void readpng_version_info() {
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

/*
 * Code taken from https://github.com/daneshih1125/xlib/blob/master/xlib_putpng.c
 */
void loadPng(FILE *file, unsigned char** data, char **clipData, unsigned int *width, unsigned int *height,
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
 * @param button widget with geometry changed
 */
void handleGeometryChanges(Widget button) {
    Display *dpy = XtDisplay(button);
    Window win = XtWindow(button);
    XImage *src_ximage = imageInfo.image;

    // Get new geometry
    XtWidgetGeometry intended,prefered;
    XtQueryGeometry(XtParent(button), &intended, &prefered);
    Dimension new_w = prefered.width;
    Dimension new_h = prefered.height;
    //printf("new_w=%d, new_h=%d\n", new_w, new_h);

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

void exposeCallback(Widget button, XtPointer xt_pointer, XtPointer xt_pointer1) {
    //printf("exposeCallback()\n");
    handleGeometryChanges(button);
    XtVaSetValues(XtParent(button), XmNwidth, imageInfo.width, XmNheight, imageInfo.height, NULL);
}

void resizeCallback(Widget button, XtPointer xt_pointer, XtPointer xt_pointer1) {
    //printf("resizeCallback()\n");
    handleGeometryChanges(button);
    //XtVaSetValues(button, XmNresizePolicy, XmRESIZE_NONE, NULL);
    //XtVaSetValues(XtParent(button), XmNallowShellResize, True, NULL);
}

bool loadPngFromFile(char *pngFile, unsigned char **data, int *png_bytes) {
    Widget button;
    char *clip = NULL;
    // Open PNG file
    char fn[128];
    sprintf(fn,"%s/%s", imageInfo.dirPath, pngFile);
    printf("Open file: %s\n", fn);
    FILE *fp = fopen(fn, "rb");
    if (!fp) {
        fprintf(stderr, "Error opening file\n");
        return false;
    }
    loadPng(fp, data, &clip, &imageInfo.width, &imageInfo.height, png_bytes);
    fclose(fp);
    return true;
}

bool createImageFromFile(Widget parent, char *pngFile, unsigned char **data, int png_bytes, XImage **image) {
    if (loadPngFromFile(pngFile, data, &png_bytes)==false) return false;

    Display *dpy = XtDisplay(parent);
    int screen = DefaultScreen(dpy);
    *image = XCreateImage (dpy, DefaultVisual(dpy, screen),
        DefaultDepth(dpy, screen), ZPixmap, 0, (char*)*data, imageInfo.width, imageInfo.height, 32, png_bytes);
    return true;
}

Widget createPixmapCanvas(Widget parent, char *fileName) {
    Widget canvas;
    Display *dpy = XtDisplay(parent);

    canvas = XtCreateManagedWidget ( "canvas", xmDrawingAreaWidgetClass, parent, NULL, 0 );
    imageInfo.canvas = canvas;
    XtAddCallback ( canvas, XmNexposeCallback, exposeCallback,  ( XtPointer )NULL );
    XtAddCallback ( canvas, XmNresizeCallback, resizeCallback, ( XtPointer )NULL );

    unsigned char *data;
    int png_bytes;
    int screen = DefaultScreen(dpy);
    imageInfo.depth = DefaultDepth(dpy, screen);

    createImageFromFile(parent, fileName, &data, png_bytes, &imageInfo.image);

    return canvas;
}

int startsWith(const char *str, const char *prefix) {
    size_t len_prefix = strlen(prefix);
    size_t len_str = strlen(str);

    // Wenn das Suchmuster länger ist als der String, kann es nicht passen
    if (len_prefix > len_str) {
        return false;
    }

    // Vergleiche die ersten 'len_prefix' Zeichen
    return strncmp(str, prefix, len_prefix) == 0;
}

int endsWith(const char *str, const char *suffix) {
    size_t len_str = strlen(str);
    size_t len_suffix = strlen(suffix);

    // Wenn das Suffix länger ist als der String, kann es nicht passen
    if (len_suffix > len_str) {
        return false;
    }

    // Setze den Zeiger an die Position, wo das Suffix im Hauptstring beginnen müsste
    return strcmp(str + (len_str - len_suffix), suffix) == 0;
}

/*
 * Timeout callback
 */
static void TimeoutCB( XtPointer client_data, XtIntervalId* id ) {
    printf("TimeoutCB\n");
    unsigned char *data;
    int png_bytes;

    char *pngFile = filenames[current];
    current++;
    if (current==maximage) {
        current=0;
    }
    XDestroyImage(imageInfo.image);
    createImageFromFile(imageInfo.shell, pngFile, &data, png_bytes, &(imageInfo.image));
    XtVaSetValues(XtParent(imageInfo.canvas), XmNwidth, imageInfo.width, XmNheight, imageInfo.height, NULL);
    handleGeometryChanges(imageInfo.canvas);
    /*
     * start time out from the beginning
     */
    XtAppAddTimeOut( imageInfo.app, TIMEOUT_NOSECONDS, TimeoutCB, NULL );
}

void main( int argc, char **argv ) {
    Widget       canvas, shell;
    XtAppContext app;

    char *baseDir = ".";
    if (argc == 2) {
        baseDir = argv[1];
    }

    readpng_version_info();

    shell = XtAppInitialize ( &app, "XPmlogo", NULL, 0,
                              &argc, argv, NULL, NULL, 0  );
    imageInfo.app = app;
    imageInfo.shell = shell;

    DIR *dir;
    struct dirent *entry;
    imageInfo.dirPath = baseDir;
    dir = opendir( imageInfo.dirPath);
    if (dir == NULL) {
        perror("Fehler beim Öffnen des Verzeichnisses");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        char  *name = entry->d_name;
        if (endsWith(name, ".png") /* || endsWith(name, ".jpg") */) {
            //printf("- %s\n", entry->d_name);
            filenames[maximage] = malloc(strlen(name)+1);
            strcpy(filenames[maximage], name);
            maximage++;
        }
    }
    closedir(dir);
    printf("Number of files: %d\n", maximage);

    canvas = createPixmapCanvas( shell,  filenames[0] );

    XtRealizeWidget ( shell );

    // Kick off timeout
    TimeoutCB( NULL, NULL );

    XtAppMainLoop ( app );
}