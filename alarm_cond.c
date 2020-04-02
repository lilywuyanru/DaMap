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

typedef struct group_id_struct
{
    struct group_id    *link;
    int                 group_id;
    int                 count; // number of alarms with the same group id
    pthread_t           *display_thread;
} group_id;

pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alarm_cond = PTHREAD_COND_INITIALIZER;
alarm_t *alarm_list = NULL;
time_t current_alarm = 0;
group_id *group_id_list = NULL;
sem_t main_semaphore;
sem_t display_sem;
// number of readers are currently being used
int reader_flag = 0;

/*
 * Insert alarm entry on list, in order.
 */
void alarm_insert (alarm_t *alarm)
{
    int status;
    alarm_t **last, *next;
    group_id *next_group_id, *new_group_id;
    int group_id_found; //indicates if the group id already exists in group id list

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

    // if group id list is empty, we add the group id to the linked list with count of 1
    if (group_id_list == NULL) {
        new_group_id = (group_id*)malloc (sizeof (group_id));
        new_group_id->group_id = alarm->group_id;
        new_group_id->count++;
        new_group_id->link = NULL;
    }
    // if group id list is not empty
    else {
        // iterate through the group id list, if we 
       for (next_group_id = group_id_list; next_group_id != NULL; next_group_id = next_group_id->link){
           // if group id for the newly inserted alarm exists in the group id list
           if(alarm->group_id== next_group_id->group_id)
                next_group_id->group_id++;     // increase the count for group id by 1
                group_id_found = 1;             // set the found group id in list variable to 1
        }
        // if group id is not found in the 
        if(group_id_found == 0) {
            // create new group id and insert to the group id list
            new_group_id = (group_id*)malloc (sizeof (group_id));
            new_group_id->group_id = alarm->group_id;
            new_group_id->count++;
            new_group_id->link = NULL;
            // points the next group id (the last group id in the list) to the newly inserted group id
            next_group_id->link = new_group_id;
        }
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
    int status, expired, group_id_found;
    group_id *next_group_id;

    /*
     * Loop forever, processing commands. The alarm thread will
     * be disintegrated when the process exits. Lock the mutex
     * at the start -- it will be unlocked during condition
     * waits, so the main thread can insert alarms.
     */
    status = sem_wait(&main_semaphore);
    if (status != 0)
        err_abort (status, "Lock mutex");
    while (1) {
        //display thread creation
        for (next_group_id = group_id_list; next_group_id != NULL; next_group_id = next_group_id->link){
            if(next_group_id->group_id == alarm->group_id) {
                next_group_id->count++;
                group_id_found = 1;
                // call display thread
                display_thread(alarm);
            }
        }
        if (group_id_found == 0) {
            pthread_t new_display_thread;
            status = pthread_create(&new_display_thread, NULL, display_thread, alarm);
            if (status != 0)
                 err_abort (status, "Create display thread");
        }      

        /*
         * If the alarm list is empty, wait until an alarm is
         * added. Setting current_alarm to 0 informs the insert
         * routine that the thread is not busy.
         */
        // current_alarm = 0;
        // while (alarm_list == NULL) {
        //     status = pthread_cond_wait (&alarm_cond, &alarm_mutex);
        //     if (status != 0)
        //         err_abort (status, "Wait on cond");
        // }

        //
        // alarm = alarm_list;
        // alarm_list = alarm->link;
        // now = time (NULL);
        // expired = 0;
        // if (alarm->time > now) {
// // #ifdef DEBUG
//             printf ("[waiting: %ld(%ld)\"%s\"]\n", alarm->time,
//                 alarm->time - time (NULL), alarm->message);
// // #endif
//             cond_time.tv_sec = alarm->time;
//             cond_time.tv_nsec = 0;
//             current_alarm = alarm->time;
//             while (current_alarm == alarm->time) {
//                 status = pthread_cond_timedwait (
//                     &alarm_cond, &alarm_mutex, &cond_time);
//                 if (status == ETIMEDOUT) {
//                     expired = 1;
//                     break;
//                 }
//                 if (status != 0)
//                     err_abort (status, "Cond timedwait");
//             }
//             if (!expired)
//                 alarm_insert (alarm);
//         } else
//             expired = 1;
//         if (expired) {
//             printf ("(%d) %s\n", alarm->seconds, alarm->message);
//             free (alarm);
//         }
    }
}

void *display_thread (alarm_t *alarm) {
    struct timespec cond_time;
    time_t now;
    int status;
    int current_group_id = alarm->group_id;        // holds group id that display thread is going to print
    int display_thread_id = pthread_self();
    alarm_t *next;
    group_id *next_group_id; 

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
        sem_wait(&display_sem);
        reader_flag++;
        if(reader_flag == 1)
            sem_wait(&main_semaphore);
        sem_post(&display_sem);

        for (next = alarm_list; next != NULL; next = next->link) {

            if (next->remove == 1) {
                printf("\nDisplay Thread %d Has Stopped Printing Message of Alarm(%d) at %ld: Group(%d) %s.",
                display_thread_id, next->alarm_id, time (NULL), next->message);
            }       
        
            //we only want to print alarms in the same group id
            if (current_group_id == next->group_id) {
                // when change variable is 0, indicates no change
                if(next->change == 0) {
                    printf("\nAlarm(%d) printed by Alarm Display Thread %d at %ld: Group(%d) %s.",
                        next->alarm_id, display_thread_id, time (NULL), next->message);
                }

                // when change variable is 1, indicates groupd id has been changed
                else if(next->change == 1) {
                    printf("\nDisplay Thread %d Has Stopped Printing Message of Alarm (%d) at %ld: Changed Group(%d) %s.",
                        display_thread_id, next->alarm_id, time (NULL), next->message);
                    next->change = 0;
                }

                // when change variable is 2, indicates groupd id has NOT been changed but message has been changed
                else if(next->change == 2) {
                    printf("\nDisplay Thread %d Starts to Print Changed Message Alarm(%d) at %ld: Group(%d) %s.",
                        display_thread_id, next->alarm_id, time (NULL), next->message);
                    next->change = 0;
                }
            }

		    sleep(5); 
        }

        // terminate display thread if no alarm has the current group id we want to print
        //loop through group id list to find the group id this display thread is currently printing, and find the display thread it corresponds to
        for (next_group_id = group_id_list; next_group_id != NULL; next_group_id = next_group_id->link){
            // if the group id in the list equals to the current group id is printing, and there is no alarm with with this group id, then terminate thread
            if (next_group_id->group_id == current_group_id && next_group_id->count == 0) { 
                pthread_join(next_group_id->display_thread, NULL);
            } 
        }

    //unlock reader
    
        sem_wait(&display_sem);
        reader_flag--;
        if(reader_flag ==0)
            sem_post(&main_semaphore);
        sem_post(&display_sem);
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

    // error message for when semaphors are not created
    if (sem_init(&main_semaphore, 0, 1) < 0) {
        printf("Error creating semaphore!\n");
        exit(1);
    }

    if (sem_init(&display_sem, 0, 1) < 0) {
        printf("Error creating semaphore!\n");
        exit(1);
    }

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


        }
    }
}
