/*
 * alarm_cond.c
 *
 * This is an enhancement to the alarm_mutex.c program, which
 * used only a mutex to synchronize access to the shared alarm
 * list. This version adds a condition variable. The alarm
 * thread waits on this condition variable, with a timeout that
 * corresponds to the earliest timer request. If the main thread
 * enters an earlier timeout, it signals the condition variable
 * so that the alarm thread will wake up and process the earlier
 * timeout first, requeueing the later request.
 */
#include <pthread.h>
#include <time.h>

#include "errors.h"
#include <ctype.h> 
#include <semaphore.h>
/*
 * The "alarm" structure now contains the time_t (time since the
 * Epoch, in seconds) for each alarm, so that they can be
 * sorted. Storing the requested number of seconds would not be
 * enough, since the "alarm thread" cannot tell how long it has
 * been on the list.
 */
sem_t sem;

typedef struct alarm_tag {
    struct alarm_tag    *link;
    int                 seconds;
    time_t              time;   /* seconds from EPOCH */
    char                message[64];
    int                 alarm_id;
    int                 group_id;
    int                 change; // change variable indicates the different messages it prints in display_thread 
    int                 remove; // remove variable 
} alarm_t;

pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alarm_cond = PTHREAD_COND_INITIALIZER;
alarm_t *alarm_list = NULL;
time_t current_alarm = 0;

/*
 * Insert alarm entry on list, in order.
 */
void alarm_insert (alarm_t *alarm)
{
    int status;
    alarm_t **last, *next;

    /*
     * LOCKING PROTOCOL:
     * 
     * This routine requires that the caller have locked the
     * alarm_mutex!
     */
    last = &alarm_list;
    next = *last;
    while (next != NULL) {
        if (next->time >= alarm->time) {
            alarm->link = next;
            *last = alarm;
            break;
        }
        last = &next->link;
        next = next->link;
    }
    /*
     * If we reached the end of the list, insert the new alarm
     * there.  ("next" is NULL, and "last" points to the link
     * field of the last item, or to the list header.)
     */
    if (next == NULL) {
        *last = alarm;
        alarm->link = NULL;
    }
#ifdef DEBUG
    printf ("[list: ");
    for (next = alarm_list; next != NULL; next = next->link)
        printf ("%ld(%ld)[\"%s\"] ", next->time,
            next->time - time (NULL), next->message);
    printf ("]\n");
#endif
    /*
     * Wake the alarm thread if it is not busy (that is, if
     * current_alarm is 0, signifying that it's waiting for
     * work), or if the new alarm comes before the one on
     * which the alarm thread is waiting.
     */
    if (current_alarm == 0 || alarm->time < current_alarm) {
        current_alarm = alarm->time;
        status = pthread_cond_signal (&alarm_cond);
        if (status != 0)
            err_abort (status, "Signal cond");
    }
}

/*
 * The alarm thread's start routine.
 */
void *alarm_thread (void *arg)
{
    alarm_t *alarm;
    struct timespec cond_time;
    time_t now;
    int status, expired;

    /*
     * Loop forever, processing commands. The alarm thread will
     * be disintegrated when the process exits. Lock the mutex
     * at the start -- it will be unlocked during condition
     * waits, so the main thread can insert alarms.
     */
    status = pthread_mutex_lock (&alarm_mutex);
    if (status != 0)
        err_abort (status, "Lock mutex");
    while (1) {
        /*
         * If the alarm list is empty, wait until an alarm is
         * added. Setting current_alarm to 0 informs the insert
         * routine that the thread is not busy.
         */
        current_alarm = 0;
        while (alarm_list == NULL) {
            status = pthread_cond_wait (&alarm_cond, &alarm_mutex);
            if (status != 0)
                err_abort (status, "Wait on cond");
            }
        alarm = alarm_list;
        alarm_list = alarm->link;
        now = time (NULL);
        expired = 0;
        if (alarm->time > now) {
// #ifdef DEBUG
            printf ("[waiting: %ld(%ld)\"%s\"]\n", alarm->time,
                alarm->time - time (NULL), alarm->message);
// #endif
            cond_time.tv_sec = alarm->time;
            cond_time.tv_nsec = 0;
            current_alarm = alarm->time;
            while (current_alarm == alarm->time) {
                status = pthread_cond_timedwait (
                    &alarm_cond, &alarm_mutex, &cond_time);
                if (status == ETIMEDOUT) {
                    expired = 1;
                    break;
                }
                if (status != 0)
                    err_abort (status, "Cond timedwait");
            }
            if (!expired)
                alarm_insert (alarm);
        } else
            expired = 1;
        if (expired) {
            printf ("(%d) %s\n", alarm->seconds, alarm->message);
            free (alarm);
        }
    }
}

void *display_thread (void *arg) {
    alarm_t *alarm;
    struct timespec cond_time;
    time_t now;
    int status;
    int group_id;        // holds group id that display thread is going to print
    int display_thread_id; //just gonna leave this for now
    alarm_t *next;

    // this is the reader writer model we are trying to implement here
    // rc ++;
    // if (rc == 1)
    // signal(mutex);
    // .
    // .  READ THE OBJECT
    // .
    // wait(mutex);
    // rc --;
    // if (rc == 0)
    // signal (wrt);
    // signal(mutex);

    while (1) { // while unchanged

       //lock reader
		status = pthread_mutex_lock (&mutex);
		if (status != 0)
		    err_abort (status, "Lock mutex");
		read_count++;
		if (read_count == 1)
		{
		    status = pthread_mutex_lock (&rw_mutex);
            if (status != 0)
                err_abort (status, "Lock mutex");
		}
		status = pthread_mutex_unlock (&mutex);
	            if (status != 0)
	                err_abort (status, "Unlock mutex");
	


        // when change variable is 0, indicates no change
        if(alarm->change == 0) {
            printf("\nAlarm(%d) printed by Alarm Display Thread %d at %ld: Group(%d) %s.",
                alarm->alarm_id, display_thread_id, time (NULL), alarm->message);
        }

        // when change variable is 1, indicates groupd id has been changed
        else if(alarm->change == 1) {
            printf("\nDisplay Thread %d Has Stopped Printing Message of Alarm (%d) at %ld: Changed Group(%d) %s.",
                display_thread_id, alarm->alarm_id, time (NULL), alarm->message);
        }

        // when change variable is 2, indicates groupd id has NOT been changed but message has been changed
        else if(alarm->change == 2) {
            printf("\nDisplay Thread %d Starts to Print Changed Message Alarm(%d) at %ld: Group(%d) %s.",
                display_thread_id, alarm->alarm_id, time (NULL), alarm->message);
        }
        
        if (alarm-> remove == 1) {
            
        }

		//unlock reader
        status = pthread_mutex_lock (&alarm_mutex);
	    if (status != 0)
	        err_abort (status, "Lock mutex");
		read_count--;
		if (read_count == 0)
		{
		    status = pthread_mutex_unlock (&rw_mutex);
	        if (status != 0)
	            err_abort (status, "Unlock mutex");
		}
		status = pthread_mutex_unlock (&mutex);
	        if (status != 0)
	            err_abort (status, "Unlock mutex");

		sleep(5); 
    }
}


int main (int argc, char *argv[])
{
    int status;
    char line[128];
    char request[100];
    char group[100];
    const char delim[2] = "(";
	char *token;
    char *command;
    char *num;
    char *id; 
    char *p;
    char *c;
    char *group_req;
    char *group_num;
    char *group_id;
    int isDigit = 0;
    int isDigitGroup = 0;
	pthread_t thread;
	const char delim1[2] = "(";
    const char delim2[2] = ")";
    alarm_t *alarm;

    sem_open("SEM", 0, 1);

    status = pthread_create (
        &thread, NULL, alarm_thread, NULL);
    if (status != 0)
        err_abort (status, "Create alarm thread");
    while (1) {
        printf ("Alarm> ");
        if (fgets (line, sizeof (line), stdin) == NULL) exit (0);
        if (strlen (line) <= 1) continue;
        alarm = (alarm_t*)malloc (sizeof (alarm_t));
        if (alarm == NULL)
            errno_abort ("Allocate alarm");

        /*
         * Parse input line into seconds (%d) and a message
         * (%64[^\n]), consisting of up to 64 characters
         * separated from the seconds by whitespace.
         * 
         * request will hold the request type as well as the alarm id
         * so will be of the form Start_Alarm(12): or Change_Alarm(12):
         * 
         * group will hold the group and id. of the form:
         * Group(12) 
         */
        if (sscanf (line, "%s %s %d %64[^\n]", 
            request, group, &alarm->seconds, alarm->message) < 1) {
            fprintf (stderr, "Bad command\n");
            free (alarm);
        } else {
            command = strtok(request, delim1);
            num = strtok(NULL, delim1);
            group_req = strtok(group, delim1);
            group_num = strtok(NULL, delim1);
            /*Check if string contains ')' and if so, split by delimeter2*/
			if (command != NULL && num != NULL && strchr(num, ')') != NULL) id = strtok(num, delim2);
            /*Check if  group string contains ')' and if so, split by delimeter2*/
            if(group_req != NULL && group_num != NULL && strchr(group_num, ')') != NULL) group_id = strtok(group_num, delim2);
            /*Check if both ids  were inputted*/
			if (id == NULL || strlen(id) == 0 || group_id == NULL || strlen(group_id) == 0) fprintf(stderr, "Bad Command\n");
            
            /*Check if command is either Change_Alarm or Start_Alarm and that Group is part of the list*/
			else if ((strcmp(command, "Change_Alarm") != 0 && strcmp(command, "Start_Alarm") != 0) || strcmp(group_req, "Group") != 0) fprintf(stderr, "Bad Command\n");
            
            else {	
				/*Check if alarm id is all numbers*/
				for (p = id; *p; p++) {
				   if (!isdigit(*p)) {
					   printf("Bad Command\n");
					   isDigit = 1;
					   break;
				   }				   
			   }

               for(c = group_id; *c; c++) {
                    if (!isdigit(*c)) {
					   printf("Bad Command\n");
					   isDigitGroup = 1;
					   break;
				    }
               }

            }
            /*if alarm id all digits, convert to number*/
            if (isDigit == 0 && isDigitGroup == 0) {
				int numid = atoi(id);
                int groupid = atoi(group_id);
                if(numid < 0 || groupid < 0){
                    printf("Bad Command\n");
                    break;
                }
				alarm->alarm_id = numid;
                alarm->group_id = groupid;
            }

            if(strcmp(command, "Change_Alarm") == 0){
                // do change alarm stuff
            } else if(strcmp(command, "Start_Alarm") == 0){
                // do the start alarm stuff
                printf("\n%s\n", command);
                printf("\n%d\n", alarm->alarm_id);
                printf("\n%d\n", alarm->group_id);
            }


            status = pthread_mutex_lock (&alarm_mutex);
            if (status != 0)
                err_abort (status, "Lock mutex");
            alarm->time = time (NULL) + alarm->seconds;
            /*
             * Insert the new alarm into the list of alarms,
             * sorted by expiration time.
             */
      
            alarm_insert (alarm);
            status = pthread_mutex_unlock (&alarm_mutex);
            if (status != 0)
                err_abort (status, "Unlock mutex");

            // printf("semaphore: %d \n", sem);

            // sem_wait(&sem);

            // sem_post(&sem);

        }
    }
}
