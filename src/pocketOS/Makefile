CROSS_COMPILE ?= arm-linux-gnueabihf-
CC    = $(CROSS_COMPILE)gcc
STRIP = $(CROSS_COMPILE)strip

TARGET = pocketOS

CFLAGS  = -O2 -Wall -DPOCKETOS_ENABLE_AUDIO \
           -marm -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard \
           -march=armv7ve \
           -I../../include -I../common

LDFLAGS = -lSDL -lSDL_ttf -lSDL_image -lSDL_mixer -lm -lz

all: $(TARGET)

$(TARGET): pocketOS.c
	$(CC) $(CFLAGS) -o $(TARGET) pocketOS.c $(LDFLAGS)
	$(STRIP) $(TARGET)

clean:
	rm -f $(TARGET)
