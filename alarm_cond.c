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
#include <semaphore.h>

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
    int                 change; // change variable indicates the different messages it prints in display_thread 
    int                 remove; // variable helps display thread know if the alarm has been removed
} alarm_t;

typedef struct group_id_struct {
    struct group_id_struct      *link;
    int                         group_id;
    int                         count; // number of alarms with the same group id
    pthread_t                   *display_thread;
} group_id;

pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alarm_cond = PTHREAD_COND_INITIALIZER;
alarm_t *alarm_list = NULL;
alarm_t *curr_alarm = NULL;
time_t current_alarm = 0;
sem_t sem;

//this will contain a list of group ids and data needed for display thread
group_id *group_id_list = NULL;


void *display_thread (void *arg) {
    printf("\n\n\nyea i get here\n\n\n");
    int status;
    pthread_t display_thread_id = pthread_self();
    int thread_group_id = -1;
    group_id *iter; 
    iter = group_id_list;

    // find group id of certain thread
    while(iter != NULL){
        printf("group id: %d", iter->group_id);
        if(*iter->display_thread == display_thread_id){
            thread_group_id = iter->group_id;
            printf("\n\n\nyea i get here 2\n\n\n");
        }
        iter = iter->link;
    }

    while(1) {
        //while the current alarm belongs to this thread
        printf("\n\n\nyea i get here 3\n\n\n");
        if (curr_alarm->group_id == thread_group_id) {
            // lock reader
            if(curr_alarm->remove == 1) {
                printf("\nDisplay Thread %d Has Stopped Printing Message of Alarm(%d) at %ld: Group(%d) %s.",
                (int)display_thread_id, curr_alarm->alarm_id, time (NULL), curr_alarm->group_id, curr_alarm->message);
                // need something like contine ?????
            }
            // when change variable is 0, indicates no change
            if(curr_alarm->change == 0) {
                printf("\nAlarm(%d) printed by Alarm Display Thread %d at %ld: Group(%d) %s.",
                    curr_alarm->alarm_id, (int)display_thread_id, time (NULL), curr_alarm->group_id, curr_alarm->message);
            }
            // when change variable is 2, indicates groupd id has NOT been changed but message has been changed
            else if(curr_alarm->change == 2) {
                printf("\nDisplay Thread %d Starts to Print Changed Message Alarm(%d) at %ld: Group(%d) %s.",
                    (int)display_thread_id, curr_alarm->alarm_id, time (NULL), curr_alarm->group_id, curr_alarm->message);
                curr_alarm->change = 0;
            }
            sleep(5);
            //unlock reader
        }
    }
}

//this method increases the count of the group given. If the group isn't in the list it will add it
void group_id_insert(group_id *group){
    group_id *next_group_id, *next;
    int group_id_found = 0; //indicates if the group id already exists in group id list
    int status;
    pthread_t disp_thread;

    // if group id list is empty, we add the group id to the linked list with count of 1
    if (group_id_list == NULL) {
        group_id_list = group;
        group_id_list->count = group_id_list->count + 1;
    }
    else {
        //iterate through the list
        for(next_group_id = group_id_list; next_group_id != NULL; next_group_id = next_group_id->link){
            //if we found a match in the list increase the count
            if(next_group_id->group_id == group->group_id){
                next_group_id->count = next_group_id->count + 1;
                group_id_found = 1; //change the boolean to found 
            }
        }
        //if there is no group id we need to make a new one in the list
        if(group_id_found == 0) {
            
            //add it to front
            group->link = group_id_list;
            group_id_list = group;
            //increase the count
            group_id_list->count = group_id_list->count+1;
            //create the display thread when a new entry is added
            status = pthread_create (&disp_thread, NULL, display_thread, NULL);
            group_id_list->display_thread = &disp_thread;
        }
    }
    #ifdef DEBUG
        printf ("[list: ");
        for (next = group_id_list; next != NULL; next = next->link)
            printf ("(group-id: %d)[count:%d], ", next->group_id,
                next->count);
        printf ("]\n");
    #endif
}

// decreases the count of the given group. If the count gets to 0 it will remove
void group_id_remove(group_id *group) {
    group_id *next_group_id, *previous, *next;

    previous = NULL;

    for(next_group_id = group_id_list; next_group_id != NULL; next_group_id = next_group_id->link){
        //if we found a match in the list decrease the count
        if(next_group_id->group_id == group->group_id){
            next_group_id->count = next_group_id->count - 1;
            //if the count reaches 0 we need to remove it
            if(next_group_id->count == 0){
                // remove the first in the list
                if(previous == NULL){
                    group_id_list = next_group_id->link;
                }
                else {
                    //remove from list
                    previous->link = next_group_id->link;
                }
            }
        }
        //move previous through the list trailing behind next group_id
        previous = next_group_id;
    }

    // #ifdef DEBUG
        printf ("[list: ");
        for (next = group_id_list; next != NULL; next = next->link)
            printf ("(group-id: %d)[count:%d], ", next->group_id,
                next->count);
        printf ("]\n");
    // #endif
}
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
        printf ("(%d)[\"%s\"] ", next->change,
            next->message);
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

void findSmallest() 
{
    alarm_t *iterator, *iter2, *smallest, *next; //temp pointers to navigate linked list
    alarm_t *prev; //holds ref to alarm before current alarm in list in order to remove alarms from the list
    int removed;

    smallest = alarm_list;
    iterator = alarm_list;
    iter2 = alarm_list;

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
    curr_alarm = smallest;
#ifdef DEBUG
    printf ("[smallest: %ld(%ld)\"%s\"]\n", curr_alarm->time,
                curr_alarm->time - time (NULL), curr_alarm->message);
#endif
    prev = NULL; //initialize the previous node ref to NULL as at the start of a list, there is no previous

    //while not at the end of the list and no node has been removed
    removed = 0;
    while(iter2 != NULL && !removed){
        //if an alarm has the same time, it has been found
        if(iter2->alarm_id == smallest->alarm_id){
            //remove from list. If it is at the start update the "HEAD" or array_list
            if(prev == NULL) {
                alarm_list = iter2->link;
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

void change_alarm (alarm_t *alarm) 
{
    int status;
    alarm_t **last, *next, *prev;
    group_id *next_group, *old, *new_group_id;
    next = alarm_list;
    prev = NULL;

    /* as long as next is not NULL and the id of next alarm is greater or equal
    * copy the message of next to be the message of alarm
    */
        while (next != NULL) {
            if (next->alarm_id == alarm->alarm_id) {
                // if the group id matches the new group id from alarm, this means the group id has not been changed
                if (next->group_id == alarm->group_id) {
                    alarm->change = 1;
                //otherwise it has been changed and we need to update the change toggle as well as update the group id list
                } else {
                    alarm->change = 2;
                    //find the old group id reference in order to change it
                    for(next_group = group_id_list; next_group != NULL; next_group = next_group->link){
                        if(next_group->group_id == next->group_id){
                            old = next_group;
                        }
                    }
                    group_id_remove(old);
                    //create a new group id and insert it into the list
                    new_group_id = (group_id*)malloc (sizeof (group_id));
                    new_group_id->group_id = alarm->group_id;
                    group_id_insert(new_group_id);
                }
                //remove from alarm_list 

                //if it is the first element
                if (prev == NULL) {
                    alarm_list = next->link;
                //otherwise remove from middle
                } else {
                    prev->link = next->link;
                }
                break;
            }
            //iterate
            prev = next;
            next = next->link;
        }

#ifdef DEBUG
    printf ("[list: ");
    for (next = alarm_list; next != NULL; next = next->link)
        printf ("(%d)[\"%s\"] ", next->change,
        next->message);
    printf ("]\n");
#endif
    //if we are changeing the current alarm
    if (curr_alarm->alarm_id == alarm->alarm_id) {
        //if we changed the current alarms group id
        if(curr_alarm->group_id != alarm->group_id) {
            printf("Display Thread <thread-id> Has Stopped Printing Message of Alarm(%d at %ld: Changed Group(%d) %s\n", 
            alarm->alarm_id, curr_alarm->time, alarm->group_id, alarm->message);
            alarm->change = 2;
            //find the old group id reference in order to change it
            for(next_group = group_id_list; next_group != NULL; next_group = next_group->link){
                if(next_group->group_id == curr_alarm->group_id){
                    old = next_group;
                }
            }
            group_id_remove(old);
            //create a new group id and insert it into the list
            new_group_id = (group_id*)malloc (sizeof (group_id));
            new_group_id->group_id = alarm->group_id;
            group_id_insert(new_group_id);
        } else {
            alarm->change = 1;
        }
        alarm_insert(alarm);
        findSmallest();
    } else {
        alarm_insert(alarm);
    }
    printf("Alarm(%d) Changed at %ld: Group(%d) %s\n", alarm->alarm_id, time (NULL), alarm->group_id, alarm->message);
}



/*
 * The alarm thread's start routine.
 */
void *alarm_thread (void *arg)
{
    alarm_t *alarm, *next;
    struct timespec cond_time;
    time_t now;
    int status, expired;
    group_id *next_group_id;

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

        curr_alarm = alarm_list;             

        findSmallest();
#ifdef DEBUG
        next = alarm_list;
        printf ("[list: ");
        for (next = alarm_list; next != NULL; next = next->link)
            printf ("(%d)[\"%s\"] ", next->change,
            next->message);
        printf ("]\n");
#endif        
        now = time (NULL);
        expired = 0;

        if (curr_alarm->time > now) {
#ifdef DEBUG
            printf ("[waiting: %d(%d)\"%s\"]\n", curr_alarm->time,
                curr_alarm->time - time (NULL), curr_alarm->message);
#endif
            cond_time.tv_sec = curr_alarm->time;
            cond_time.tv_nsec = 0;
            current_alarm = curr_alarm->time;
            while (current_alarm == curr_alarm->time) {
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
                alarm_insert(curr_alarm);
        } else
            expired = 1;
        if (expired) {
            //find the corresponding group id for the group id being printed and remove it
            for(next_group_id = group_id_list; next_group_id != NULL; next_group_id = next_group_id->link){
                if(next_group_id->group_id == curr_alarm->group_id){
                    group_id_remove(next_group_id);
                }
            }
            printf ("(%d) %s\n", curr_alarm->seconds, curr_alarm->message);
            free (curr_alarm);
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
    char *group_holder;
    int isDigit = 0;
    int isDigitGroup = 0;
	pthread_t thread;
	const char delim1[2] = "(";
    const char delim2[2] = ")";
    alarm_t *alarm;
    group_id *new_group_id;

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
            if(group_req != NULL && group_num != NULL && strchr(group_num, ')') != NULL) group_holder = strtok(group_num, delim2);
            /*Check if both ids  were inputted*/
			if (id == NULL || strlen(id) == 0 || group_holder == NULL || strlen(group_holder) == 0) fprintf(stderr, "Bad Command\n");
            
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

               for(c = group_holder; *c; c++) {
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
                int groupid = atoi(group_holder);
                if(numid < 0 || groupid < 0){
                    printf("Bad Command\n");
                    break;
                }
				alarm->alarm_id = numid;
                alarm->group_id = groupid;
            }
#ifdef DEBUG
            printf("%s\n",command);
#endif
            if(strcmp(command, "Change_Alarm") == 0){
                // do change alarm stuff
                status = pthread_mutex_lock (&alarm_mutex);
                if (status != 0)
                    err_abort (status, "Lock mutex");
                alarm->time = time (NULL) + alarm->seconds;
                change_alarm (alarm);
                status = pthread_mutex_unlock (&alarm_mutex);
                if (status != 0)
                    err_abort (status, "Unlock mutex");
            } else if(strcmp(command, "Start_Alarm") == 0){
                printf("\nAlarm(%d) Inserted by Main Thread %d Into Alarm List at %ld: Group(%d) %s\n", alarm->alarm_id, (int)&thread, time (NULL), alarm->group_id, alarm->message);
                status = pthread_mutex_lock (&alarm_mutex);
                if (status != 0)
                    err_abort (status, "Lock mutex");
                alarm->time = time (NULL) + alarm->seconds;
                alarm->change = 0;
                /*
                * Insert the new alarm into the list of alarms,
                * sorted by expiration time.
                */
                alarm_insert (alarm);
                new_group_id = (group_id*)malloc (sizeof (group_id));
                new_group_id->group_id = alarm->group_id;
                new_group_id->link = NULL;
                group_id_insert(new_group_id);

                status = pthread_mutex_unlock (&alarm_mutex);
                if (status != 0)
                    err_abort (status, "Unlock mutex");
            }
        }
    }
}
