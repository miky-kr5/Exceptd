TARGET = exceptd
CFLAGS = -O3 -Wall `mysql_config --include`
LDLIBS = `mysql_config --libs`

all: exceptd.o
	$(CC) -o $(TARGET) $< $(CFLAGS) $(LDLIBS)

exceptd.o: exceptd.c

debug: CFLAGS = -g -Wall -D_DEBUG `mysql_config --include --cflags`
debug: all

clean:
	$(RM) $(TARGET) *.o
