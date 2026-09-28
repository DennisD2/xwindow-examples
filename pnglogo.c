/********************************************************************
 * This example shows how to load a PNG and use it as a pixmap. Uses libpng.
 * The code loads the PNG file, creates a pixmap definition feasible for XpmCreatePixmapFromData()
 * and displays the pixmap in a PushButton widget.
 * The conversion to a pixmap is slow, because it iterates over all pixels in a for(){ ... for(){...}} construct.
 * See pnglogo2.c for a much faster implementation.
 * ******************************************************************/

#include <Xm/Xm.h>
#include <Xm/PushB.h>
#include <X11/xpm.h>

#include "png.h"
#include "zlib.h"
#include "stdlib.h"

Widget createPixmapCanvas (Widget parent, char *pngFile);

void readpng_version_info() {
    fprintf(stderr, "   Compiled with libpng %s; using libpng %s.\n",
      PNG_LIBPNG_VER_STRING, png_libpng_ver);
    fprintf(stderr, "   Compiled with zlib %s; using zlib %s.\n",
      ZLIB_VERSION, zlib_version);
}

Widget createPixmapCanvas(Widget parent, char *pngFile) {
    Widget button;
    Pixmap pix = None;
    Pixmap mask = None;
    Display *dpy = XtDisplay(parent);
    int status;
    XpmAttributes attributes;

    button = XtCreateManagedWidget("button", xmPushButtonWidgetClass, parent, NULL, 0);

    Pixel bg_color;
    XtVaGetValues ( button,
                XmNdepth,    &attributes.depth,
                XmNcolormap, &attributes.colormap,
                XmNbackground, &bg_color,
                NULL);
    unsigned char bg_r = (bg_color >> 16) & 0xFF;
    unsigned char bg_g = (bg_color >> 8)  & 0xFF;
    unsigned char bg_b =  bg_color        & 0xFF;

    /*
     * Specify the visual to be used and set the XpmAttributes mask.
     */
    attributes.visual = DefaultVisual ( dpy, DefaultScreen ( dpy ) );
    attributes.valuemask = XpmDepth | XpmColormap | XpmVisual;

    // Open PNG file
    FILE *fp = fopen(pngFile, "rb");
    if (!fp) {
        fprintf(stderr, "Error opening file\n");
        return button;
    }

    // Initialize libpng
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) { fclose(fp); return button; }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) { png_destroy_read_struct(&png_ptr, NULL, NULL); fclose(fp); return button; }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return button;
    }

    png_init_io(png_ptr, fp);
    png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_STRIP_16 | PNG_TRANSFORM_PACKING | PNG_TRANSFORM_EXPAND, NULL);

    int width = png_get_image_width(png_ptr, info_ptr);
    int height = png_get_image_height(png_ptr, info_ptr);
    png_bytep *row_pointers = png_get_rows(png_ptr, info_ptr);
    int channels = png_get_channels(png_ptr, info_ptr);

    // Set up XPM array
    int num_colors = width * height;
    int xpm_lines = 1 + num_colors + height;
    char **xpm_data = malloc(xpm_lines * sizeof(char *));

    int tokenSize = 4;
    if (num_colors > 456976 /* 29^4 */) {
        tokenSize=5;
    }
    printf("Token size: %d\n", tokenSize);

    // write header line
    xpm_data[0] = malloc(50);
    sprintf(xpm_data[0], "%d %d %d %d", width, height, num_colors, tokenSize);

    // create color palette and pixel array
    int color_index = 0;
    for (int y = 0; y < height; y++) {
        xpm_data[1 + num_colors + y] = malloc(width * tokenSize + 1);
        xpm_data[1 + num_colors + y][0] = '\0';

        for (int x = 0; x < width; x++) {
            png_bytep px = &(row_pointers[y][x * channels]);
            unsigned char r = px[0];
            unsigned char g = px[1];
            unsigned char b = px[2];

            // create char token for xpm
            char token[10];
            if (tokenSize == 5) {
                sprintf(token, "%c%c%c%c%c",
                        'a' + (color_index / 456976) % 26,
                        'a' + (color_index / 17576) % 26,
                        'a' + (color_index / 676) % 26,
                        'a' + (color_index / 26) % 26,
                        'a' + color_index % 26);
            } else if (tokenSize == 4) {
                sprintf(token, "%c%c%c%c",
                    'a' + (color_index / 17576) % 26,
                    'a' + (color_index / 676) % 26,
                    'a' + (color_index / 26) % 26,
                    'a' + color_index % 26);
            }

            xpm_data[1 + color_index] = malloc(50);

            // get alpha value (if available) and normalize 0.0..1,0
            float alpha = (channels == 4) ? (px[3] / 255.0f) : 1.0f;

            // alpha = 0 -> draw background
            // alpha = 1 -> draw pixel
            // alpha in between: mix in some transparency
            unsigned char final_r = (unsigned char)(r * alpha + bg_r * (1.0f - alpha));
            unsigned char final_g = (unsigned char)(g * alpha + bg_g * (1.0f - alpha));
            unsigned char final_b = (unsigned char)(b * alpha + bg_b * (1.0f - alpha));

            sprintf(xpm_data[1 + color_index], "%s c #%02X%02X%02X", token, final_r, final_g, final_b);

            // add token
            strcat(xpm_data[1 + num_colors + y], token);
            color_index++;
        }
    }

    // create Pixmap from data
    status = XpmCreatePixmapFromData(dpy, DefaultRootWindow ( dpy ),
                                     xpm_data, &pix, &mask, &attributes);

    // cleanup
    for (int i = 0; i < xpm_lines; i++) {
       free(xpm_data[i]);
    }
    free(xpm_data);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);

    // set pixmap in widget
    if (status == XpmSuccess && pix != None) {
        XtVaSetValues(button,
                      XmNlabelType, XmPIXMAP,
                      XmNlabelPixmap, pix,
                      NULL);

    } else {
        fprintf(stderr, "XPM-Fehler: Pixmap konnte nicht erstellt werden (%d).\n", status);
    }
    return button;
}
                            

void main ( int argc, char **argv ) {
    Widget       shell, button;
    XtAppContext app;

    char *pngFile = "moon.png";
    if (argc == 2) {
        pngFile = argv[1];
    }

    readpng_version_info();

    shell = XtAppInitialize ( &app, "XPmlogo", NULL, 0,
                              &argc, argv, NULL, NULL, 0  );

    button = createPixmapCanvas( shell, pngFile );

    XtRealizeWidget ( shell );
    XtAppMainLoop ( app );
}