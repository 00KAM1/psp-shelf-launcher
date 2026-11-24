# Shelf - a home screen for a PSP library (ARK-5). MIT licensed; lodepng is zlib-licensed.
TARGET = shelf
OBJS = src/app.o src/sys.o src/ui.o src/gfx.o src/scan.o src/iso.o src/sfo.o src/image.o \
       src/data.o src/arkcfg.o src/util.o src/text.o src/jfont_data.o src/lodepng.o

CFLAGS = -O2 -G0 -Wall -Wno-unused-parameter -std=gnu99 \
         -DLODEPNG_NO_COMPILE_ENCODER -DLODEPNG_NO_COMPILE_DISK -DLODEPNG_NO_COMPILE_ANCILLARY_CHUNKS -DLODEPNG_NO_COMPILE_CPP
ASFLAGS = $(CFLAGS)

LIBS = -lpspdebug -lpspge -lpsppower -lpsprtc -lpspusb -lpspusbstor -lpspsystemctrl_user -larkctrl -lpspkubridge

BUILD_PRX = 1
PSP_FW_VERSION = 660
PSP_LARGE_MEMORY = 0

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Shelf
PSP_EBOOT_ICON = res/ICON0.PNG
PSP_EBOOT_PIC1 = res/PIC1.PNG

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

$(OBJS): $(wildcard src/*.h)
