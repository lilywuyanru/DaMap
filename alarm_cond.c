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
/*
 * The "alarm" structure now contains the time_t (time since the
 * Epoch, in seconds) for each alarm, so that they can be
 * sorted. Storing the requested number of seconds would not be
 * enough, since the "alarm thread" cannot tell how long it has
 * been on the list.
 */
typedef struct alarm_tag {
    struct alarm_tag    *link;
    int                 seconds;
    time_t              time;   /* seconds from EPOCH */
    char                message[64];
    int                 alarm_id;
    int                 group_id;
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
        if (next->alarm_id >= alarm->alarm_id) {
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
// #ifdef DEBUG
    printf ("[list: ");
    for (next = alarm_list; next != NULL; next = next->link)
        printf ("%ld(%ld)[\"%s\"] ", next->time,
            next->time - time (NULL), next->message);
    printf ("]\n");
// #endif
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
    alarm_t *alarm; //holds alarm that will be looked at next to print
    alarm_t *iterator, *iter2, *tmp; //temp pointers to navigate linked list
    alarm_t *prev; //holds ref to alarm before current alarm in list in order to remove alarms from the list
    struct timespec cond_time;
    time_t now;
    int status, expired, removed;

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
        alarm = alarm_list; //initialize these pointers to the start of the list
        tmp = alarm_list;
        iter2 = alarm_list;
        iterator = alarm_list;
        prev = NULL; //initialize the previous node ref to NULL as at the start of a list, there is no previous
        removed = 0;
        //while not at the end of the list
        while(iterator->link != NULL){
            //if there is an alarm that expires sooner, remove it from the list
            if(iterator->time < tmp->time){
                tmp = iterator;
            }
            iterator = tmp->link;
        }
        //if there is the only one node in the list remove it. 
        alarm = tmp;
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
            // if (!expired)
                // alarm_insert (alarm);
        } else
            expired = 1;
        if (expired) {
            printf ("(%d) %s\n", alarm->seconds, alarm->message);
            if(iter2->link == NULL){
                alarm_list = NULL;
            } else {
                //while not at the end of the list and no node has been removed
                while(iter2->link!=NULL && !removed){
                    //if an alarm has the same time, it has been found
                    if(iter2->time == alarm->time){
                        //remove from list. If it is at the start update the "HEAD" or array_list
                        if(prev == NULL){
                            alarm_list = alarm->link;
                        //if not at start, remove by changing the previous link to the node after the node being removed
                        } else {
                            prev->link = iter2->link;
                        }
                        //update boolean to stop searching
                        removed = 1;
                    }
                    //continue traversing the list
                    prev = iter2;
                    iter2 = iter2->link;
                }
            }
            free (alarm);
        }
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
            }
            
        }
    }
}
