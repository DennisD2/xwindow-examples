
#include <stdio.h>
#include "gifdec.h"

#include <X11/Xlib.h>
#include <stdlib.h>
#include <stdint.h>

#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <X11/xpm.h>
#include <X11/Intrinsic.h>
#include <X11/extensions/Xrender.h>

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
#
ImageInfo imageInfo;

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

XImage *gifCanvasToImageFast2(Display *display, Visual *visual, unsigned int depth, gd_GIF *gif,
    int *w, int *h) {
    if (!gif || !gif->canvas || !gif->palette) {
        // Falls das Display kein 32-Bit nutzt, müsste man Fallbacks einbauen.
        // 32-Bit (ZPixmap) ist auf modernen Linux/X11 Systemen der absolute Standard.
        return NULL;
    }
    if (depth != 32 || depth != 24) {
        printf("gifCanvasToImageFast: depth must be 32 bits, but is %d\n", depth);
    }

    int width = gif->width;
    int height = gif->height;
    *w = width;
    *h = height;

    // 1. Speicher für das XImage allokieren (4 Bytes pro Pixel bei 32-Bit)
    uint32_t *image_data = (uint32_t *)malloc(width * height * sizeof(uint32_t));
    if (!image_data) return NULL;

    XImage *ximage = XCreateImage(
        display, visual, depth, ZPixmap, 0,
        (char *)image_data, width, height, 32, 0
    );

    if (!ximage) {
        free(image_data);
        return NULL;
    }

    // 2. OPTIMIERUNG: Lookup-Table (LUT) für die Palette vorberechnen
    // Ein GIF hat maximal 256 Farben. Wir wandeln diese 256 RGB-Werte vorab
    // in das exakte Bit-Format des X-Servers um.
    uint32_t color_lut[256];
    int num_colors = gif->palette->size;

    for (int i = 0; i < num_colors; i++) {
        uint8_t r = gif->palette->colors[i * 3 + 0];
        uint8_t g = gif->palette->colors[i * 3 + 1];
        uint8_t b = gif->palette->colors[i * 3 + 2];

        // Bit-Shifts basierend auf den Masken des X-Visuals einmalig berechnen
        color_lut[i] = ((r * visual->red_mask  / 255) & visual->red_mask)  |
                       ((g * visual->green_mask / 255) & visual->green_mask) |
                       ((b * visual->blue_mask / 255) & visual->blue_mask);
    }

    // 3. OPTIMIERUNG: Direkter Speicherzugriff ohne XPutPixel
    // Wir nutzen flache Zeiger und überlassen der CPU sequenzielle Speicherzugriffe.
    uint8_t *src = gif->canvas;
    uint32_t *dst = image_data;
    int total_pixels = width * height;

    // Diese Schleife lässt sich vom Clang-Compiler extrem gut per SIMD (Auto-Vektorisierung) optimieren
    for (int i = 0; i < total_pixels; i++) {
        dst[i] = color_lut[src[i]];
    }

    return ximage;
}

// Hilfsfunktion zur Ermittlung des Bit-Shifts aus einer X11-Maske
static int get_shift(unsigned long mask) {
    if (mask == 0) return 0;
    int shift = 0;
    while ((mask & 1) == 0) {
        mask >>= 1;
        shift++;
    }
    return shift;
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
    XImage *ximage = XCreateImage(
        display,
        visual,
        depth,
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

    // 3. Konvertierungsschleife: Palette indizieren und Pixel für Pixel an XImage übergeben
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            // Index des aktuellen Pixels im GIF-Canvas ermitteln
            int canvas_index = y * width + x;
            uint8_t color_idx = gif->canvas[canvas_index];

            // RGB-Werte aus der GIF-Palette holen
            uint8_t r = gif->palette->colors[color_idx * 3 + 0];
            uint8_t g = gif->palette->colors[color_idx * 3 + 1];
            uint8_t b = gif->palette->colors[color_idx * 3 + 2];

            // RGB-Werte in das Pixelformat des X-Visuals/Bildschirms packen
            // Xlib verwendet oft das Format 0x00RRGGBB (oder BGR je nach System)
            unsigned long pixel_value = 0;

            if (visual->red_mask == 0) {
                // Fallback für alte 8-Bit Pseudocolor Displays (seltener Spezialfall)
                pixel_value = color_idx;
            } else {
                // Standard TrueColor Maskierung (Shift-Logik basierend auf dem X-Server-Visual)
                // Dies stellt sicher, dass Rot, Grün und Blau im richtigen Byte landen.
                pixel_value = ((r * visual->red_mask  / 255) & visual->red_mask)  |
                              ((g * visual->green_mask / 255) & visual->green_mask) |
                              ((b * visual->blue_mask / 255) & visual->blue_mask);
            }

            // Sicherer Xlib-Befehl, um das formatierte Pixel in den Speicher zu schreiben
            XPutPixel(ximage, x, y, pixel_value);
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

XImage *load_gif_to_ximage(Display *dpy, Visual *visual, unsigned int depth, const char *filename,
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
    int end=0;
    XImage *image = NULL;
    while (gd_get_frame(gif) && !end) {
        frame_count++;
        end=1;
        // gif->canvas enthält jetzt die rohen Pixeldaten des aktuellen Frames
        image = gifCanvasToImageSlow(dpy, visual, depth, gif, w, h);
        if (image == NULL) {
            printf("Image cannot be created from file\n");
        }
    }
    printf("Anzahl der Frames: %d\n", frame_count);

    // Speicher freigeben
    gd_close_gif(gif);

    return image;
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
    //printf("TimeoutCB\n");
    //XtVaSetValues(XtParent(imageInfo.canvas), XmNwidth, imageInfo.width, XmNheight, imageInfo.height, NULL);
    //handleGeometryChanges(imageInfo.canvas);
    /*
     * start time out from the beginning
     */
    XtAppAddTimeOut( imageInfo.app, TIMEOUT_NOSECONDS, TimeoutCB, NULL );
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


