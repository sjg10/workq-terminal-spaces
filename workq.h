/*
 * workq.h
 *
 * This header file defines the interfaces for a "work queue"
 * manager. A "manager object" is created with several
 * parameters, including the required size of a work queue
 * entry, the maximum desired degree of parallelism (number of
 * threads to service the queue), and the address of an
 * execution engine routine.
 *
 * The application requests a work queue entry from the manager,
 * fills in the application-specific fields, and returns it to
 * the queue manager for processing. The manager will create a
 * new thread to service the queue if all current threads are
 * busy and the maximum level of parallelism has not yet been
 * reached.
 *
 * The manager will dequeue items and present them to the
 * processing engine until the queue is empty; at that point,
 * processing threads will begin to shut down. (They will be
 * restarted when work appears.)
 */
#include <pthread.h>
#define NOCHANGE 0x5112012
#define WORKQ_VALID     0xdec1992

#define MAX_Q_LENGTH 100000
#define NO_OF_THREADS  4
#define CTRL_THREAD_WAIT_TIME 1


/*
 * Structure to keep track of work queue requests.
 */
typedef struct workq_ele_tag {
    struct workq_ele_tag        *next;
    void                        *data;
} workq_ele_t;

/*
 * Structure describing a work queue.
 */
typedef struct workq_tag {
    pthread_mutex_t     mutex;
    pthread_cond_t      cv;             /* wait for work */
    pthread_attr_t      attr;           /* create detached threads */
    workq_ele_t         *first, *last;  /* work queue */
    int                 queue_length;
    int                 file_counter;
    int                 valid;          /* set when valid */
    int                 quit;           /* set when workq should quit */
    int                 parallelism;    /* number of threads required */
    int                 counter;        /* current number of threads */
    int                 idle;           /* number of idle threads */
    void                (*engine)(void *arg);   /* user engine */
    void                (*save_element)(void *arg,FILE *file); /*dumping function*/
    void*                (*load_element)(FILE *file); /*dumping function*/
} workq_t;

/*
 * Define work queue functions
 */
extern int workq_init (
    workq_t     *wq,
    int         threads,                                  /* maximum threads */
    void        (*engine)(void *),                       /* engine routine */
    void         (*save_element)(void* arg,FILE *file), /*save routine*/
    void*         (*load_element)(FILE *file));          /*load routine*/
extern int workq_destroy (workq_t *wq);
extern int workq_add (workq_t *wq, void *data);
extern int workq_load_unload(workq_t *wq);
