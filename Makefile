NAME=pingcheck
OBJS=main.o icmp.o ping.o util.o ubus.o uci.o scripts.o
LIBS=-lubus -lubox -luci
CFLAGS+=-std=gnu99 -Wall -Wextra -g -I.

all: $(NAME)

$(NAME): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

check:
	sparse $(CFLAGS) -D__linux__ *.[ch]

clean:
	-rm -f *.o *~
	-rm -f $(NAME)
