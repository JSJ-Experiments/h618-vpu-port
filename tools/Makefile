CC ?= cc
CFLAGS ?= -O2 -g
WARNINGS := -Wall -Wextra -Werror

GST_CFLAGS := $(shell pkg-config --cflags gstreamer-codecparsers-1.0)
GST_LIBS := -Wl,--no-as-needed -lgstcodecs-1.0 \
	$(shell pkg-config --libs gstreamer-codecparsers-1.0)

.PHONY: all clean

all: vp9-controls-dump vp9-request-sequence vp9-request-smoke

vp9-controls-dump: vp9-controls-dump.c \
		gstreamer-compat/gst/codecs/gstvp9statefulparser.h \
		gstreamer-compat/gst/codecs/codecs-prelude.h
	$(CC) $(CFLAGS) $(WARNINGS) \
		-Igstreamer-compat $(GST_CFLAGS) $< -o $@ $(GST_LIBS)

vp9-request-sequence: vp9-request-sequence.c
	$(CC) $(CFLAGS) $(WARNINGS) $< -o $@

vp9-request-smoke: vp9-request-smoke.c
	$(CC) $(CFLAGS) $(WARNINGS) $< -o $@

clean:
	$(RM) vp9-controls-dump vp9-request-sequence vp9-request-smoke
