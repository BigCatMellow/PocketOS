CROSS_COMPILE ?= arm-linux-gnueabihf-
CC    = $(CROSS_COMPILE)gcc
STRIP = $(CROSS_COMPILE)strip
READELF = $(CROSS_COMPILE)readelf

TARGET = pocketOS

CFLAGS  = -O2 -Wall -Werror -DPOCKETOS_ENABLE_AUDIO \
           -fno-pie \
           -marm -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard \
           -march=armv7ve \
           -I../../include -I../common

DEVICE_LDFLAGS = -no-pie
LDFLAGS = -L../../lib -lSDL -lSDL_ttf -lSDL_image -lSDL_mixer -lm -lz -lsqlite3

all: $(TARGET) verify

$(TARGET): pocketOS.c
	$(CC) $(CFLAGS) -o $(TARGET) pocketOS.c $(DEVICE_LDFLAGS) $(LDFLAGS)
	$(STRIP) $(TARGET)

verify: $(TARGET)
	@$(READELF) -h $(TARGET) | grep -q 'Type:.*EXEC' || \
		{ echo 'ERROR: $(TARGET) is PIE; the Miyoo firmware requires a non-PIE executable.'; exit 1; }
	@bad_glibc="$$($(READELF) --version-info $(TARGET) | \
		sed -n 's/.*Name: GLIBC_\([0-9][0-9.]*\).*/\1/p' | \
		awk -F. '$$1 > 2 || ($$1 == 2 && $$2 > 17) { print; exit }')"; \
		if [ -n "$$bad_glibc" ]; then \
			echo "ERROR: $(TARGET) requires GLIBC_$$bad_glibc; Miyoo-compatible builds must not exceed GLIBC_2.17."; \
			exit 1; \
		fi

clean:
	rm -f $(TARGET)

.PHONY: all clean verify
