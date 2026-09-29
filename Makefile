CC = gcc 
DEBUG = -O
CFLAGS = $(DEBUG)

LIBS = -lXpm -lXm -lXt -lX11 #-lPW
LIBS_PNG = -lpng -lz
LIBS_RENDER = -lXrender
LIBS_JPEG = -ljpeg

TARGETS=pnglogo pnglogo2 viewer jviewer gviewer

all: $(TARGETS)

pnglogo: pnglogo.o
	$(CC) $(CFLAGS) -o $@  $@.o $(LIBS) $(LIBS_PNG)
pnglogo2: pnglogo2.o
	$(CC) $(CFLAGS) -o $@  $@.o $(LIBS) $(LIBS_PNG) $(LIBS_RENDER)
viewer: viewer.o
	$(CC) $(CFLAGS) -o $@  $@.o $(LIBS) $(LIBS_PNG) $(LIBS_RENDER)
jviewer: jviewer.o
	$(CC) $(CFLAGS) -o $@  $@.o $(LIBS) $(LIBS_PNG) $(LIBS_RENDER) $(LIBS_JPEG)
gviewer: gviewer.o gifdec.o
	$(CC) $(CFLAGS) -o $@  $@.o gifdec.o $(LIBS) $(LIBS_RENDER)
clean:
	rm -f *.o *~* *.a 
	rm -f $(TARGETS)

