
#include <stdio.h>
#include "gifdec.h"

#include <X11/Xlib.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <X11/xpm.h>
#include <X11/Intrinsic.h>
#include <X11/extensions/Xrender.h>

#define TIMEOUT_NOSECONDS 200L
int timeout = TIMEOUT_NOSECONDS;

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


#define MAX_FRAMES 500

typedef struct {
    XImage *frames[MAX_FRAMES];
    int delays[MAX_FRAMES]; // Speichert, wie lange jeder Frame sichtbar sein soll (in ms)
    int frame_count;
    int width;
    int height;
} GifAnimation;

GifAnimation anim;

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

void dumpFrames(GifAnimation *a) {
    printf("%d Frames, all %dx%d\n", a->frame_count, a->width, a->height);
    for (int i=0; i < a->frame_count; i++) {

        printf("Frame %d:\n", i, a->width, a->height);
        printf("   delay=%d\n", a->delays[i]);
        XImage * xi = a->frames[i];
        printf("   width=%d, height=%d\n", xi->width, xi->height);
        printf("   depth=%d\n", xi->depth);
        printf("   data=0x%lx\n", xi->data);

        printf("imageInfo:\n");
        printf("   width=%d, height=%d\n", imageInfo.width, imageInfo.height);
        printf("   depth=%d\n", imageInfo.depth);
        if (imageInfo.image != NULL) {
            printf("   image=0x%lx\n", imageInfo.image->data);
        }
    }
}

XImage *gifCanvasToImageSlow(Display *display, Visual *visual, unsigned int depth, gd_GIF *gif,
    int *w, int *h) {
    if (!gif || !gif->canvas || !gif->palette) {
        return NULL;
    }

    int width = gif->width;
    int height = gif->height;
    *w = width;
    *h = height;

    // 1. Speicher für die rohen Bilddaten des XImage reservieren (z. B. 4 Bytes pro Pixel bei 32-Bit Tiefe)
    // Bei TrueColor/ZPixmap wird empfohlen, den Puffer dynamisch zu erzeugen.
    int bytes_per_pixel = (depth <= 8) ? 1 : ((depth <= 16) ? 2 : 4);
    char *image_data = malloc(width * height * bytes_per_pixel);
    if (!image_data) {
        return NULL;
    }

    // 2. Die XImage-Struktur initialisieren
    // ZPixmap sorgt dafür, dass die Pixel zeilenweise hinterlegt sind (Scanlines)
    int screen = DefaultScreen(display);
    XImage *ximage = XCreateImage(
        display,
        DefaultVisual(display, screen),
        DefaultDepth(display, screen),
        ZPixmap,
        0,
        image_data,
        width,
        height,
        32,       // Bitmap-Padding (üblich sind 32 Bit)
        0         // bytes_per_line auf 0 setzen: Xlib berechnet es automatisch
    );

    if (!ximage) {
        free(image_data);
        return NULL;
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int canvas_index = 3*(y * width + x);
            uint8_t r = gif->canvas[canvas_index+0];
            uint8_t g = gif->canvas[canvas_index+1];
            uint8_t b = gif->canvas[canvas_index+2];
            Pixel pixel = (r<<16)|(g<<8)|b;
            XPutPixel(ximage, x, y, pixel);
        }
    }
    return ximage;
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

    //dumpFrames(&anim);
    //printf("src width=%d, height=%d\n", src_ximage->width, src_ximage->height);
    //printf("img width=%d, height=%d\n", imageInfo.image->width, imageInfo.image->height);
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
    //XtVaSetValues(XtParent(button), XmNwidth, imageInfo.width, XmNheight, imageInfo.height, NULL);
    //XtVaSetValues(button, XmNresizePolicy, XmRESIZE_NONE, NULL);
    //XtVaSetValues(XtParent(button), XmNallowShellResize, True, NULL);
}

XImage *load_gif_to_ximageOLD(Display *dpy, Visual *visual, unsigned int depth, const char *filename,
    int *w, int *h) {
    // GIF-Datei öffnen
    printf("Open file %s\n", filename);
    gd_GIF *gif = gd_open_gif(filename);
    if (!gif) {
        printf("Fehler beim Öffnen der Datei %s.\n", filename);
        return NULL;
    }

    printf("Breite: %dpx, Höhe: %dpx\n", gif->width, gif->height);

    // Loop durch alle Frames der Animation
    int frame_count = 0;
    XImage *image = NULL;
    while (gd_get_frame(gif)) {
        frame_count++;
        //gif->frame
        // gif->canvas enthält jetzt die rohen Pixeldaten des aktuellen Frames

    }
    image = gifCanvasToImageSlow(dpy, visual, depth, gif, w, h);
    if (image == NULL) {
        printf("Image cannot be created from file\n");
    }
    printf("Anzahl der Frames: %d\n", frame_count);

    // Speicher freigeben
    gd_close_gif(gif);

    return image;
}

int maxframes = 0;
int currentframe = 0;

XImage *load_gif_to_ximage(Display *dpy, Visual *visual, unsigned int depth, const char *filename,
    int *w, int *h) {
    gd_GIF *gif = gd_open_gif(filename);
    if (!gif) return NULL;

    anim.width = gif->width;
    anim.height = gif->height;
    *w = gif->width;
    *h = gif->height;
    anim.frame_count = 0;

    // Schleife läuft durch alle Frames
    while (gd_get_frame(gif) && anim.frame_count < MAX_FRAMES) {
        // 1. Konvertiere das aktuelle Canvas in ein XImage und speichere es im Array
        anim.frames[anim.frame_count] = gifCanvasToImageSlow(dpy, visual, depth, gif, &anim.width, &anim.height);

        // 2. Speicher die Frame-Verzögerung (gif->gce.delay ist in Hundertstelsekunden, daher * 10 für Millisekunden)
        anim.delays[anim.frame_count] = gif->gce.delay * 10;
        printf("Frame %d, delay=%d\n", anim.frame_count, anim.delays[anim.frame_count]);

        anim.frame_count++;
    }
    maxframes = anim.frame_count;
    printf("maxframes: %d\n", maxframes);

    gd_close_gif(gif);
    dumpFrames(&anim);
    // Gibt zum Beispiel den ersten Frame als Startbild zurück
    imageInfo.image = anim.frames[0];
    imageInfo.width = anim.width;
    imageInfo.height = anim.height;
    imageInfo.depth = anim.frames[currentframe]->depth;
    currentframe=0;

    return anim.frames[0];
}

Widget createPixmapCanvas(Widget parent, char *fileName) {
    Widget canvas;
    Display *dpy = XtDisplay(parent);

    canvas = XtCreateManagedWidget ( "canvas", xmDrawingAreaWidgetClass, parent, NULL, 0 );
    imageInfo.canvas = canvas;
    XtAddCallback ( canvas, XmNexposeCallback, exposeCallback,  ( XtPointer )NULL );
    XtAddCallback ( canvas, XmNresizeCallback, resizeCallback, ( XtPointer )NULL );

    int screen = DefaultScreen(dpy);
    imageInfo.depth = DefaultDepth(dpy, screen);

    if (endsWith(fileName, ".gif")) {
        Display *dpy = XtDisplay(imageInfo.shell);
        Visual *v = DefaultVisual(dpy, DefaultScreen(dpy));
        imageInfo.image = load_gif_to_ximage(dpy, v, imageInfo.depth, fileName,
            &(imageInfo.width), &(imageInfo.height));
    }
    return canvas;
}

static void TimeoutCB( XtPointer client_data, XtIntervalId* id ) {
    //printf("TimeoutCB, currentframe=%d\n", currentframe);

    //XtVaSetValues(XtParent(imageInfo.canvas), XmNwidth, imageInfo.width, XmNheight, imageInfo.height, NULL);
    //handleGeometryChanges(imageInfo.canvas);

    //XDestroyImage(imageInfo.image);
    imageInfo.image = anim.frames[currentframe];
    imageInfo.width = anim.width;
    imageInfo.height = anim.height;
    imageInfo.depth = anim.frames[currentframe]->depth;
    timeout = anim.delays[currentframe];

    currentframe++;
    if (currentframe == maxframes) {
        currentframe = 0;
    }
    currentframe=0;

    XtVaSetValues(XtParent(imageInfo.canvas), XmNwidth, imageInfo.width, XmNheight, imageInfo.height, NULL);
    handleGeometryChanges(imageInfo.canvas);

    /*
     * start time out from the beginning
     */
    XtAppAddTimeOut( imageInfo.app, timeout, TimeoutCB, NULL );
}

int main( int argc, char **argv ) {
    Widget       canvas, shell;
    XtAppContext app;

    shell = XtAppInitialize ( &app, "XPmlogo", NULL, 0,
                              &argc, argv, NULL, NULL, 0  );
    imageInfo.app = app;
    imageInfo.shell = shell;

    //char *file = "test-images/dilbert.gif";
    char *file = "test-images/halbes_pferd.gif";
    if (argc > 1) {
        file = argv[1];
    }
    canvas = createPixmapCanvas(shell, file);

    XtRealizeWidget ( shell );

    // Kick off timeout
    TimeoutCB( NULL, NULL );

    XtAppMainLoop ( app );
}


