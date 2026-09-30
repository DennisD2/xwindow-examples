/********************************************************************
* This example shows how to load a GIF file and use it as a pixmap. Uses libXrender.
 * The code loads the GIF file with 1..MAX_FRAMES frames (if animated GIF),
 * creates according number of XIMage definitions.
 * Then animation is started by displaying all frames loaded in a loop.
 * Each frames XImage is used to set the pixmap in a Canvas widget.
 * Because XRender extension is used for scaling and an XImage structure is used,
 * the rendering is *very* fast.
 * ******************************************************************/
#include <stdio.h>
#include "gifdec.h"

#include <X11/Xlib.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <Xm/MainW.h>
#include <X11/xpm.h>
#include <X11/Intrinsic.h>
#include <X11/extensions/Xrender.h>

#define MAX_FRAMES 500

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
    //int timeout;
    int state;
} AppInfo;

AppInfo appInfo;

typedef struct {
    XImage *frames[MAX_FRAMES];
    int delays[MAX_FRAMES];
    int frame_count;
    int width;
    int height;
    int currentframe;

} GifAnimation;

GifAnimation anim;

int endsWith(const char *str, const char *suffix) {
    size_t len_str = strlen(str);
    size_t len_suffix = strlen(suffix);

    if (len_suffix > len_str) {
        return false;
    }
    return strcmp(str + (len_str - len_suffix), suffix) == 0;
}

void dumpFrames(GifAnimation *a) {
    printf("%d Frames, all %dx%d\n", a->frame_count, a->width, a->height);
    for (int i=0; i < a->frame_count; i++) {

        printf("Frame %d:\n", i, a->width, a->height);
        printf("   delay=%d\n", a->delays[i]);
        XImage * xi = a->frames[i];
        printf("   width=%d, height=%d\n", xi->width, xi->height);
        printf("   depth=%d\n", xi->depth);
        printf("   data=0x%lx\n", xi->data);

        //printf("imageInfo:\n");
        //printf("   width=%d, height=%d\n", imageInfo.width, imageInfo.height);
        //printf("   depth=%d\n", imageInfo.depth);
        //if (imageInfo.image != NULL) {
        //    printf("   image=0x%lx\n", imageInfo.image->data);
        //}
    }
}

XImage *gifCanvasToImage(Display *display, Visual *visual,
    gd_GIF *gif , uint8_t *rgbBuffer, unsigned int depth,
    int *w, int *h) {
    if (!gif || !gif->canvas || !gif->palette) {
        return NULL;
    }

    int width = gif->width;
    int height = gif->height;
    *w = width;
    *h = height;

    int bytes_per_pixel = (depth <= 8) ? 1 : ((depth <= 16) ? 2 : 4);
    char *image_data = malloc(width * height * bytes_per_pixel);
    if (!image_data) {
        return NULL;
    }

    int screen = DefaultScreen(display);
    XImage *ximage = XCreateImage(display, visual, DefaultDepth(display, screen),
        ZPixmap, 0,
        image_data, width, height,
        32, 0  // 0 means xlib calculates it
    );

    if (!ximage) {
        free(image_data);
        return NULL;
    }

    // copy rgb values to image
    //memcpy(ximage->data, rgbBuffer, width * height * bytes_per_pixel);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int canvas_index = 3*(y * width + x);
            uint8_t r = rgbBuffer[canvas_index+0];
            uint8_t g = rgbBuffer[canvas_index+1];
            uint8_t b = rgbBuffer[canvas_index+2];
            Pixel pixel = (r<<16)|(g<<8)|b;
            XPutPixel(ximage, x, y, pixel);
        }
    }
    return ximage;
}

/**
 * Handles geometry changes. uses XRender extension for fast scaling.
 * @param canvas widget with geometry changed
 */
void handleGeometryChanges(Widget canvas) {
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

void exposeCallback(Widget canvas, XtPointer xt_pointer, XtPointer xt_pointer1) {
    //printf("exposeCallback()\n");
    handleGeometryChanges(canvas);
    //XtVaSetValues(imageInfo.shell, XmNwidth, imageInfo.width, XmNheight, imageInfo.height, NULL);
}

void resizeCallback(Widget canvas, XtPointer xt_pointer, XtPointer xt_pointer1) {
    //printf("resizeCallback()\n");
    handleGeometryChanges(canvas);
}

XImage *loadGIFtoXImage(Display *dpy, Visual *visual, unsigned int depth, const char *filename,
    int *w, int *h) {
    gd_GIF *gif = gd_open_gif(filename);
    if (!gif) {
        printf("Unable to load GIF file %s. Exiting.\n", filename);
        exit(1);
    }
    anim.width = gif->width;
    anim.height = gif->height;
    *w = gif->width;
    *h = gif->height;
    anim.frame_count = 0;

    // loop trough all frames in GIF
    while (gd_get_frame(gif) && anim.frame_count < MAX_FRAMES) {
        uint8_t *rgbBuffer = malloc(anim.width * anim.height * 4);
        gd_render_frame(gif, rgbBuffer);

        anim.frames[anim.frame_count] = gifCanvasToImage(dpy, visual, gif, rgbBuffer, depth, &anim.width, &anim.height);

        // delay value is in 1/100s units - 10ms . Multiply with 10 to get value in milliseconds
        // This value is needed by XtAppAddTimeOut()
        anim.delays[anim.frame_count] = gif->gce.delay * 10;
        printf("Frame %d\n", anim.frame_count);

        anim.frame_count++;
    }

    gd_close_gif(gif);
    dumpFrames(&anim);

    imageInfo.image = anim.frames[0];
    imageInfo.width = anim.width;
    imageInfo.height = anim.height;
    imageInfo.depth = anim.frames[anim.currentframe]->depth;
    anim.currentframe=0;

    return anim.frames[0];
}

Widget createPixmapCanvas(Widget parent, char *fileName) {
    Widget canvas;
    Display *dpy = XtDisplay(parent);

    canvas = XtCreateManagedWidget ( "canvas", xmDrawingAreaWidgetClass, parent, NULL, 0 );
    appInfo.canvas = canvas;
    XtAddCallback ( canvas, XmNexposeCallback, exposeCallback,  ( XtPointer )NULL );
    XtAddCallback ( canvas, XmNresizeCallback, resizeCallback, ( XtPointer )NULL );

    int screen = DefaultScreen(dpy);
    imageInfo.depth = DefaultDepth(dpy, screen);

    if (endsWith(fileName, ".gif")) {
        Display *dpy = XtDisplay(appInfo.shell);
        Visual *v = DefaultVisual(dpy, DefaultScreen(dpy));
        imageInfo.image = loadGIFtoXImage(dpy, v, imageInfo.depth, fileName,
            &(imageInfo.width), &(imageInfo.height));
    }
    return canvas;
}

static void TimeoutCB( XtPointer client_data, XtIntervalId* id ) {
    //printf("TimeoutCB, currentframe=%d\n", currentframe);

    // Get time value for XtAppAddTimeOut()
    imageInfo.image = anim.frames[anim.currentframe];
    int frameTimeout = anim.delays[anim.currentframe];

    if (appInfo.state==S_PAUSE) {
        XtAppAddTimeOut( appInfo.app, frameTimeout, TimeoutCB, NULL );
        return;
    }

    imageInfo.width = anim.width;
    imageInfo.height = anim.height;

    if (anim.frames[anim.currentframe] != NULL) {
        imageInfo.depth = anim.frames[anim.currentframe]->depth;

        anim.currentframe++;
        if (anim.currentframe == anim.frame_count) {
            anim.currentframe = 0;
        }

        XtVaSetValues(XtParent(appInfo.canvas), XmNwidth, imageInfo.width, XmNheight, imageInfo.height, NULL);
        handleGeometryChanges(appInfo.canvas);
        /*
         * start time out from the beginning
        */
        XtAppAddTimeOut( appInfo.app, frameTimeout, TimeoutCB, NULL );
    }
}

static void canvasButtonEventHandler (Widget w, XtPointer clientData, XEvent *event, Boolean *flag ) {
    //printf("canvasButtonEventHandler\n");
    if (appInfo.state == S_NORMAL) {
        appInfo.state = S_PAUSE;
        printf("paused\n");
    } else {
        appInfo.state = S_NORMAL;
        printf("normal\n");
    }
}

int main( int argc, char **argv ) {
    XtAppContext app;

    Widget shell = XtAppInitialize ( &app, "XPmlogo", NULL, 0,
                              &argc, argv, NULL, NULL, 0  );
    appInfo.app = app;
    appInfo.state = S_NORMAL;

    Widget mainWindow = XtCreateManagedWidget ( "mainWindow",
                                         xmMainWindowWidgetClass,
                                         shell, NULL, 0 );
    appInfo.shell = shell;
    //appInfo.timeout = 0;

    char *file = "test-images/halbes_pferd.gif";
    if (argc > 1) {
        file = argv[1];
    }

    anim.currentframe = 0;
    Widget canvas = createPixmapCanvas(mainWindow, file);
    XtAddEventHandler ( canvas, ButtonPressMask, FALSE,
                    canvasButtonEventHandler, NULL );
    // Now we have correct size of gif
    XtVaSetValues(mainWindow, XmNwidth, anim.width, XmNheight, anim.height, NULL);

    XtRealizeWidget ( shell );

    // Kick off timeout
    TimeoutCB( NULL, NULL );

    XtAppMainLoop ( app );
}
