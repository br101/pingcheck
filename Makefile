NAME		= pingcheck

SRC		= main.c
SRC		+= icmp.c
SRC		+= ping.c
SRC		+= util.c
SRC		+= ubus.c
SRC		+= uci.c
SRC		+= scripts.c
SRC		+= tcp.c

LIBS		= -lubus -lubox -luci

INCLUDES	+= -I.
CFLAGS		+=-std=gnu99 -Wall -Wextra -g

all: bin
clean:
check:

include Makefile.default
