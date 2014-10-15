TARGET = exceptd
CFLAGS = -O3 -Wall `mysql_config --include`
LDLIBS = `mysql_config --libs`

all: $(TARGET)

$(TARGET): exceptd.o daemonize.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDLIBS)

exceptd.o: exceptd.c

daemonize.o: daemonize.c daemonize.h

debug: CFLAGS = -g -Wall -D_DEBUG `mysql_config --include --cflags`
debug: all

clean:
	$(RM) $(TARGET) *.o
