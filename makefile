new_alarm_cond : new_alarm_cond.c
	cc new_alarm_cond.c -D_POSIX_PTHREAD_SEMANTICS -lpthread
	./a.out