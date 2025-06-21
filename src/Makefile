XBE_TITLE = Type D
SRCS      = $(CURDIR)/main.c
NXDK_DIR  ?= $(CURDIR)/../..
NXDK_SDL       = y
NXDK_SDL_TTF   = y
NXDK_SDL_IMAGE = y
NXDK_SDL_AUDIODRV = dsp

SRCS += \
    $(CURDIR)/detect.c send_cmd.c
CFLAGS += -I$(CURDIR)/src

include $(NXDK_DIR)/Makefile