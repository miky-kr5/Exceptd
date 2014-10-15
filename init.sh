#! /bin/bash
### BEGIN INIT INFO
# Provides: excetpd
# Required-Start: $network $syslog $mysql
# Required-Stop: $network $syslog $mysql
# Default-Start: 2 3 4 5
# Default-Stop: 0 1 6
# Short-Description: A java exception report daemon.
### END INIT INFO

start() {
    PID=`pidof exceptd`
    if [ $? -eq 0 ]
    then
	echo "exceptd already started."
	return
    else
	echo "Starting exceptd."
	exceptd &
	echo "Done."
    fi
}

stop() {
    PID=`pidof exceptd`
    if [ $? -eq 0 ]
    then
	echo "Stopping exceptd."
        kill -2 `pidof exceptd`
        echo "Done."
    else
	echo "exceptd already stopped."
    fi
}

case "$1" in
    start)
        start
        ;;
    stop)
        stop
        ;;
    status)
	PID=`pidof exceptd`
	if [ $? -eq 0 ]
	then
	    echo "exceptd is running."
	else
	    echo "exceptd is down."
	fi
        ;;
    restart)
        stop
        start
        ;;
    *)
        echo "Usage:  {start|stop|restart|status}"
        exit 1
        ;;
esac
exit $?
