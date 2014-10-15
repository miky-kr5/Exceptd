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

install: $(TARGET)
	cp $(TARGET) /sbin/$(TARGET)
	cp init.sh /etc/init.d/$(TARGET)
	chmod 755 /etc/init.d/$(TARGET)
	update-rc.d exceptd defaults

uninstall:
	update-rc.d -f exceptd remove
	$(RM) /sbin/$(TARGET) /etc/init.d/$(TARGET)

clean:
	$(RM) $(TARGET) *.o
