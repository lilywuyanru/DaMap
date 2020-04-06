#include <pthread.h>
#include <time.h>

#include "errors.h"
#include <ctype.h> 
#include <semaphore.h>

void *alarm_thread(void *arg)
{
    alarm_t *alarm;					//Pointer to current alarm in linked list
    int sleep_time;					//Time shortest alarm has to wait
    time_t now;						//Current time
    int status;						//Mutex lock status
    alarm_t *shortest_alarm;		//Alarm that ends to soonest
    alarm_t *prev_shortest_alarm;	//The Alarm before the shortest alarm found in the list of alarms
    alarm_t *prev_alarm;			//The Alarn befire the current alarm in the linked list 

    /*
     * Loop forever, processing commands. The alarm thread will
     * be disintegrated when the process exits.
     */
    while (1)
    {
        /* Lock the mutex at the start of this alarm, so that nothing
		 * else can access the its resources once this is engaged any 
		 * other thread that runs into this line will stop here until 
		 * this is unlocked Should only unlock before sleeping or yeilding
         */
        // lock at the beginning to keep noone else from accessing
        status = pthread_mutex_lock(&alarm_mutex);
        if (status != 0)
        {
            err_abort(status, "Lock mutex");
        }
        // if the alarm_list is empty
        if (alarm_list == NULL)
        {
            // wait for new alarms, it requires that the mutex is locked
			//it will then unlock mutex and waits for the condition condition
			// variable (alarm condition) to be signalled and then relocks mutex
            pthread_cond_wait(&alarm_condition, &alarm_mutex);
        }
        alarm = alarm_list;
        shortest_alarm = alarm;	//initialize the shortest alarm to current alarm as we start searching through the list
								//because the first alarm is the only alarm
        prev_alarm = alarm;		//initialize the prev alarm to current alarm as we start searching through the list
								//because we don't know if there's a before yet
								
        // searches through the list of alarms for the shortest alarm
        if (alarm != NULL) //If there is an alarm
        {
            while (alarm != NULL) //Iterates to find shortest
            {                     /*test code
                // printf("%d(%d)[\"%s\"] ", alarm->seconds,
                //        alarm->seconds - time(NULL), alarm->message);
                // printf("]\n");*/

                //If the current alarm ends sooner than shorest
                if (alarm->time < shortest_alarm->time)
                { //set it to new shortest
                    shortest_alarm = alarm;
                    prev_shortest_alarm = prev_alarm;
                }
                // FIND NEW GUY AND SAVE IT, IF ANY
                if (alarm->thread_index == -1)
                {
                    //locks to find new guy so no other thread can take the processor
                    status = pthread_mutex_lock(&alarm_display_mutex);
                    if (status != 0)
                    {
                        err_abort(status, "Lock mutex");
                    }
                    // ASSIGN THE NEW GUY A NEW ALARM THREAD
                    // find the smallest alarm thread
                    int smallest_display_thread = 0;

                    for (int i = 0; i < 3; i++)
                    { //If find thread with least count set its index
                        if (display_thread_count[i] < display_thread_count[smallest_display_thread])
                        {
                            smallest_display_thread = i;
                        }
                    }

                    // create a new thread if there are less than 3 threads
                    if (display_thread_count[smallest_display_thread] == 0)
                    { //Creates the server thread that will process all our display threads
                        status = pthread_create(&display_threads[smallest_display_thread], NULL, display_alarm_thread, NULL);
                        if (status != 0)
                        {
                            err_abort(status, "Create display thread failure");
                        }
                        printf("\nAlarm Thread Created New Display Alarm Thread %i For Alarm(%i) at %ld: %d %s\n",
                               (int)display_threads[smallest_display_thread], alarm->alarm_id, time(NULL), alarm->seconds, alarm->message);
                    }
                    else //If there are 3 threads display them
                    {
                        printf("\nAlarm Thread Display Alarm Thread %i Assigned to Display Alarm(%i) at %ld: %d %s\n",
                               (int)display_threads[smallest_display_thread], alarm->alarm_id, time(NULL), alarm->seconds, alarm->message);
                    }

                    // increase count of thread
                    display_thread_count[smallest_display_thread]++;
                    alarm->thread = (int)display_threads[smallest_display_thread];
                    alarm->thread_index = smallest_display_thread;
                    //printf("%i %i %i\n", display_thread_count[0], display_thread_count[1], display_thread_count[2]);
                    status = pthread_mutex_unlock(&alarm_display_mutex);
                    if (status != 0)
                    {
                        err_abort(status, "Unlock mutex");
                    }
                }
                prev_alarm = alarm;
                alarm = alarm->link;
            }
        }

        // if shortest alarm is not done yet
        if (shortest_alarm->time > time(NULL))
        {
            struct timespec timeout;
            timeout.tv_sec = shortest_alarm->time;
            timeout.tv_nsec = 0;
            // timed wait; continue if time is over OR if a new alarm is added
            pthread_cond_timedwait(&alarm_condition, &alarm_mutex, &timeout);
        }
        // if alarm expired, remove it, otherwise check for new alarms
        if (shortest_alarm != NULL && shortest_alarm->time <= time(NULL))
        {
            printf("\nAlarm Thread Removed Alarm(%i) at %ld: %d %s\n",
                   shortest_alarm->alarm_id,
                   time(NULL),
                   shortest_alarm->seconds,
                   shortest_alarm->message);
            //lock so that we can remove a display thread uninterrupted
            status = pthread_mutex_lock(&alarm_display_mutex);
            if (status != 0)
            {
                err_abort(status, "Lock mutex");
            }
            // decrease thread count as display thread is removed
            if (--display_thread_count[shortest_alarm->thread_index] == 0)
            {
                printf("\nAlarm Thread Terminated Display Thread %i at %ld", (int)shortest_alarm->thread, time(NULL));
            }
            //printf("%i %i %i\n", display_thread_count[0], display_thread_count[1], display_thread_count[2]);
            //unlocks now that its finished removing the display thread

            status = pthread_mutex_unlock(&alarm_display_mutex);
            if (status != 0)
            {
                err_abort(status, "Unlock mutex");
            }
            shortest_alarm->thread = 0; // change added by Abjeet Wed 5:12 Pm on feb 19
            // removing from singly linked list
            // removing head
            if (shortest_alarm == alarm_list)
            {
                alarm_list = alarm_list->link;
                free(shortest_alarm);
            }
            // removing tail
            else if (shortest_alarm->link == NULL)
            {
                prev_shortest_alarm->link = NULL;
                free(shortest_alarm);
            }
            // remove middle nodes
            else
            {
                prev_shortest_alarm->link = shortest_alarm->link;
                free(shortest_alarm);
            }
        }
        /* The mutix has to be unlocked before yielding.
		 * if not then the main can not add new alarms to the list
		 * ruining the Async goal of Alarm_Mutex.c. Assuming status 
		 * is not zero and there is not any sleep time then it will 
		 * sched_yield which will yield the processor to a thread that
		 * is ready unless there are no ready threads, allowing us to 
		 * process a user input if there is one or does nothing if not
		*/
        status = pthread_mutex_unlock(&alarm_mutex);
        if (status != 0)
        {
            err_abort(status, "Unlock mutex");
        }

        // #ifdef DEBUG
        //         printf("[waiting: %d(%d)\"%s\"]\n", alarm->time,
        //                sleep_time, alarm->message);
        // #endif
    }
}