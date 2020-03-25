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
    char                message[128];
    int                 alarm_id;
    int                 group_id;
    int                 changed;
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
#ifdef DEBUG
    printf ("[list: ");
    for (next = alarm_list; next != NULL; next = next->link)
        printf ("%ld(%ld)[\"%s\"] ", next->time, alarm->time - time (NULL), next->message);
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

void alarm_change(alarm_t *new_values){
    
    int found = 0;
    alarm_t **last, *next, *prev, *head, **l, *n;
    int old_group_id;
    int count = 0;
    /*
    * LOCKING PROTOCOL:
    * 
    * This routine requires that the caller have locked the
    * alarm_mutex!
    */
    last = &alarm_list;
    next = *last;
    //this will change the alarm specified
    while (next != NULL && found == 0) {
        printf("next: %d, alarm: %d\n", next->alarm_id, new_values->alarm_id);
        if (next->alarm_id == new_values->alarm_id) {
            old_group_id = next->group_id;
            strcpy(next->message, new_values->message);
            next->seconds = new_values->seconds;
            *last = next;
            found = 1;
        }
        last = &next->link;
        next = next->link;
    }
    
    last = &alarm_list;
    next = *last;
    while(next != NULL){
        count++;
        last = &next->link;
        next = next->link;
    }
    l = &alarm_list;
    head = *l;
    n = *l;
    prev = NULL;
    //if the group id was changed we need to change all the alarms with that group id and readd them
    if(new_values->group_id != old_group_id){
        //if there is only one element in the list
        if(n->link == NULL){
            //if the only item has the changed group id, delete and readd
            if(n->group_id == old_group_id){
                //remove alarm
                head = NULL;
                n->group_id = new_values->group_id;
                // alarm_insert(n);
            }
        //if there is more than one element
        } else {
            //while not at the end
            for(int i = 0; i < count; i++){
                if(n->group_id == old_group_id){
                    //if the first element needs to be removed
                    if(prev == NULL){
                        // head = head->link;
                        n->group_id = new_values->group_id;
                        // alarm_insert(n);
                    } else {
                        // prev->link = n->link;
                        n->group_id = new_values->group_id;
                        // alarm_insert(n);
                    }
                }
                l = &n->link;
                prev = n;
                n = n->link;
            }
        }
    }

    // #ifdef DEBUG
        printf ("[list: ");
        for (next = alarm_list; next != NULL; next = next->link)
            printf ("%d(%d)[\"%s\"] group_id:%d ", next->seconds, next->alarm_id, next->message, next->group_id);
        printf ("]\n");
    // #endif
}

/*
 * The alarm thread's start routine.
 */
void *alarm_thread (void *arg)
{
    alarm_t *alarm;
    alarm_t *iterator, *iter2, *smallest; //temp pointers to navigate linked list
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

        alarm = alarm_list;

        smallest = alarm_list;
        iter2 = alarm_list;
        iterator = alarm_list;
        
        // alarm_list = alarm->link;        
        prev = NULL; //initialize the previous node ref to NULL as at the start of a list, there is no previous
        removed = 0;

        while(iterator->link != NULL){
            //if there is an alarm that expires sooner, set smallest to that alarm
            if(iterator->time < smallest->time){
                smallest = iterator;
            }
            iterator = iterator->link;
        }
        if(iterator->time < smallest->time){
                smallest = iterator;
        }

        alarm = smallest;
        
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
                printf ("current alarm: %s\n", alarm->message);
                if (status == ETIMEDOUT) {
                    expired = 1;
                    break;
                }
                if (status != 0)
                    err_abort (status, "Cond timedwait");
            }
        } else {
            expired = 1;
        }
        if (expired) {
            printf ("(%d) %s %d\n", alarm->seconds, alarm->message, alarm->group_id);
            free (alarm);
                    if(iter2->link == NULL){
            alarm_list = NULL;
        } else {
            //while not at the end of the list and no node has been removed
            removed = 0;
            while(iter2 != NULL && !removed){
                //if an alarm has the same id, it has been found
                if(iter2->alarm_id == smallest->alarm_id){
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
                if(iter2->link != NULL) {
                    prev = iter2;
                    iter2 = iter2->link;
                }
            }
        }
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
    int numid;
    int groupid;
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
        if (sscanf (line, "%s %s %d %64[^\n]", request, group, &alarm->seconds, alarm->message) < 1) {
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
				numid = atoi(id);
                groupid = atoi(group_id);
                if(numid < 0 || groupid < 0){
                    printf("Bad Command\n");
                    break;
                }
				alarm->alarm_id = numid;
                alarm->group_id = groupid;
            }

            if(strcmp(command, "Change_Alarm") == 0){
                // do change alarm stuff
                alarm->changed = 1;
                alarm_change(alarm);
            } else if(strcmp(command, "Start_Alarm") == 0){
                alarm->changed = 0;
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
