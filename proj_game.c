/*
 * proj_game.c
 *
 *
 * We apply operations to branch a known terminal weighted projective space to
 * others. It is conjectured that iteration of this process will terminate 
 * and hence we can gain the space of maximal degree. We add each branching to
 * a work queue that then uses threads to branch each of these until we
 * terminate.
 *
 * The number of cases is LARGE and so we must dump the cases to disk as we
 * progress, to return to at a later time.
 */
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include "workq.h"
#include "errors.h"


typedef struct projective_space {
    struct projective_space *link;
    int         k;
    long int         A[5];
    long int         minA[5][2];
} proj_space;

//function to save a proj space to a file
void proj_space_save(void *arg, FILE *file){
    proj_space *element=(proj_space*)arg;
    fwrite_unlocked(element, sizeof(proj_space), 1, file);
    free(element);
}

void *proj_space_load(FILE *file){
    int read;
    proj_space *element = (proj_space*) malloc(sizeof(proj_space));
    read=fread_unlocked(element,sizeof(proj_space),1,file);
    if(read!=1)
        element=NULL;
    return (void*)element;
}

typedef struct engine_tag {
    struct engine_tag   *prev;
    struct engine_tag   *next;
    pthread_t           thread_id;
    pthread_mutex_t    my_list_mutex;
    proj_space *list_first, *list_last;
} engine_t;

typedef struct list_ends{
    proj_space *first,*last;}list_ends;

/* Keep track of active engines */
pthread_key_t engine_key;       
engine_t *engine_list_head = NULL;
pthread_mutex_t engine_list_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Keep track of the highest k has been */
pthread_mutex_t max_k_mutex = PTHREAD_MUTEX_INITIALIZER;
int max_k;
workq_t workq;


/*
 * This is the maths that will calculate all branches
 * of a given element, and if that branch terminates will
 * also update the max_k achieved if a new max_k is achieved.
 */
list_ends branch(proj_space *arg){
    //We are going to return a pointers of the start and end of a linked list
    //of new branches we will then go through.
    //branch should live in a pool
    int i,j;
    long int dmin,dmax;
    //create first element to use.
    list_ends stack_ends;
    stack_ends.first=NULL;
    stack_ends.last=NULL;
    proj_space* temp_element=(proj_space*)malloc(sizeof(proj_space));
    if (temp_element==NULL){
        err_abort (ENOMEM, "Pool out of memory");
        }
    //start thinking:
    long int h=arg->A[0]+arg->A[1]+arg->A[2]+arg->A[3]+arg->A[4];
    dmin=arg->k+3-h;
    dmax=arg->k+4-h;
    
    //find which A[i] we are allowed to increase,
    //using the condition from minA[i]
    char s;
    char s_temp;
    char idx;
    idx=0;
    //if an A[i] is a viable candidate for increment, lets flag it in idx
    for(i=0,j=1;i<5;i++,j*=2){
        if( (arg->A[i])*(arg->minA[i][1]) < (arg->minA[i][0])*(arg->k+1) ) idx+=j;}

    //let's iterate through subsets of A:
    for(s=0;s<32;s++){
       s_temp=s;
       //count the size of this subset
       for (j = 0; s_temp; j++)
            s_temp &= s_temp - 1;
        
        //and check whether this subset is a subset of idx and is correct size
        if ( ((idx | ~s) == -1) && (j<=dmax) && (j>=dmin) ){
            for(i=0;i<5;i++)
                temp_element->A[i]=arg->A[i];
            s_temp=s;
            for(i=0;i<5;i++){
                if(s_temp % 2)
                    temp_element->A[i]++;
                s_temp>>=1;}
            //is ordering preserved?
            if(temp_element->A[0]<=temp_element->A[1] &&
                temp_element->A[1]<=temp_element->A[2] &&
                temp_element->A[2]<=temp_element->A[3] &&
                temp_element->A[3]<=temp_element->A[4]){
                //calculate the new minA
                for(i=0;i<5;i++){
                    j=(((arg->minA[i][0])*((arg->k)+1)) < ((arg->minA[i][1])*(temp_element->A[i])));//minA or newA/k+1, which is smaller?
                    temp_element->minA[i][0]=( (j) ? (arg->minA[i][0]) : (temp_element->A[i]) );
                    temp_element->minA[i][1]=( (j) ? (arg->minA[i][1]) : ((arg->k)+1) );
                }
                //tell it its k value, and say it is at the end of the list.
                temp_element->k=(arg->k)+1;
                temp_element->link=NULL;
                if(stack_ends.first==NULL)
                    stack_ends.first=temp_element;
                else{stack_ends.last->link=temp_element;}
                stack_ends.last=temp_element;//save it!
                temp_element=(proj_space*)malloc(sizeof(proj_space));
                if (temp_element==NULL){
                    err_abort (ENOMEM, "Pool out of memory");
                }
            }
        }
    }
    free(temp_element);
    //if first_new_element is empty here the branch has terminated, lets possibly update max_k:
    if(stack_ends.first==NULL){
        pthread_mutex_lock(&(max_k_mutex));
        if(max_k<arg->k){
            max_k=arg->k;
            printf("The highest k terminated at so far is %d\n",max_k);
            printf("on (%ld,%ld,%ld,%ld,%ld)\n",arg->A[0],arg->A[1],arg->A[2],arg->A[3],arg->A[4]);
            printf("with (%ld/%ld,%ld/%ld,%ld/%ld,%ld/%ld,%ld/%ld)\n",
                arg->minA[0][0],arg->minA[0][1],arg->minA[1][0],arg->minA[1][1],
                arg->minA[2][0],arg->minA[0][1],arg->minA[3][0],arg->minA[3][1],
                arg->minA[4][0],arg->minA[4][1]);
        }
        pthread_mutex_unlock(&(max_k_mutex));
    }
    
    return stack_ends;
}

/*
 * Thread-specific data destructor routine for engine_key,
 * to update list of engines if one is destroyed.
 */
void destructor (void *value_ptr)
{   
    engine_t *engine = (engine_t*)value_ptr;
    pthread_mutex_lock (&engine_list_mutex);
    //printf("Engine %p destroyed\n",engine);
    //remove engine from list.
    if (engine->next!=NULL && engine->prev!=NULL){
        //engine is in middle of list
        engine->next->prev=engine->prev;
        engine->prev->next=engine->next;
    }
    else if(engine->next!=NULL){
        //engine is not at end of list
        engine->next->prev=NULL;
        engine_list_head=engine->next;
        }
    else if(engine->prev!=NULL){
        //engine is not at top of list
        engine->prev->next=NULL;
        }
    else{
        //we have no more engines!
        engine_list_head=NULL;
        }
    pthread_mutex_unlock (&engine_list_mutex);
    free(engine);
}

/*
 * This is the routine called by the work queue servers to
 * perform operations in parallel.
 */
void engine_routine (void *arg){
    engine_t *engine;
    list_ends new_list;
    proj_space *temp;
    int *returns;
    int i;
    int status;
    //see what engine is running
    engine = pthread_getspecific (engine_key);
    //if engine is new, prepare the engine:
    if (engine == NULL) {
        engine = (engine_t*)malloc (sizeof (engine_t));
        status=pthread_mutex_init(&(engine->my_list_mutex),NULL);
        if (status != 0)
            err_abort (status, "Mutex initialisation");
        pthread_mutex_lock(&(engine->my_list_mutex));
        //lets add this new engine to the engine list:
        engine->prev=NULL;
        pthread_mutex_lock(&engine_list_mutex);
        if(engine_list_head==NULL){
            engine_list_head=engine;
            engine->next=NULL;
            }
        else{
            engine->next=engine_list_head;
            engine_list_head->prev=engine;
            engine_list_head=engine;
        }
        pthread_mutex_unlock(&engine_list_mutex);
        //initialize other properties.
        status = pthread_setspecific (
            engine_key, (void*)engine);
        if (status != 0)
            err_abort (status, "Set tsd");
        engine->thread_id = pthread_self ();
        pthread_mutex_unlock(&(engine->my_list_mutex));
    }
    pthread_mutex_lock(&(engine->my_list_mutex));
    engine->list_first=(proj_space*)arg;
    engine->list_last=(proj_space*)arg;
    temp=engine->list_first;
    pthread_mutex_unlock(&(engine->my_list_mutex));
    while(temp){
        new_list = branch(temp);//get new_list as branches from current element.
        pthread_mutex_lock(&(engine->my_list_mutex));
        //now did we get any new entries?
        if(new_list.first != NULL){
            //our list branched, we are going to stick the new_list at the end of current list:
            engine->list_last->link=new_list.first;
            engine->list_last=new_list.last;
        }
        //now increment to next on list
        temp=engine->list_first->link;
        free(engine->list_first);
        engine->list_first=temp;
        pthread_mutex_unlock(&(engine->my_list_mutex)); 
    }
}
    
/*
 * Thread start routine that issues the initial work queue requests.
 * and then coordinates between the workq and each engines personal
 * list.
 */
void *control_thread_routine (void *arg)
{   engine_t *current_engine,*temp_engine;
    engine_t *current_head;
    //initialise first space:
    proj_space *element,*temp_element;
    int status;
    int i,j;
    element = (proj_space*)malloc (sizeof (proj_space));
    element->link=NULL;
    for(i=0;i<5;i++){
        element->A[i]=1;
        element->minA[i][0]=1;
        element->minA[i][1]=2;}
    element->k=2;
    max_k=2;
    status = workq_add (&workq, (void*)element);
    if (status != 0)
        err_abort (status, "Add to work queue");


    sleep(1);//allow first engine to grow a list
    pthread_mutex_lock(&engine_list_mutex);
    current_head=engine_list_head;
    pthread_mutex_unlock(&engine_list_mutex);

    //this scans the engines:
    while(current_head != NULL){
        pthread_mutex_lock(&engine_list_mutex);
        current_head=engine_list_head;
        current_engine=current_head;
        while (current_engine != NULL) {
            pthread_mutex_lock(&(current_engine->my_list_mutex));
            //don't want to mess with top of list as this may be in use,
            //so lets see if there are at least two in list
            if( current_engine->list_first!=NULL ){
                while(current_engine->list_first->link!=NULL){
                    temp_element=current_engine->list_first->link;
                    current_engine->list_first->link=current_engine->list_first->link->link;
                    temp_element->link=NULL;
                    status = workq_add (&workq, (void*)temp_element);
                    if (status != 0)
                        err_abort (status, "Add to work queue");  
                    }
            }
            current_engine->list_last=current_engine->list_first;
            temp_engine = current_engine->next;
            pthread_mutex_unlock(&(current_engine->my_list_mutex));
            current_engine=temp_engine;
        }
        pthread_mutex_unlock(&engine_list_mutex);
        status=workq_load_unload(&workq);
        if (status==NOCHANGE)
            sleep(CTRL_THREAD_WAIT_TIME);
        else if (status!=0)
            err_abort (status, "File dump");
    }
    return NULL;
}


int main (int argc, char *argv[])
{   //create a control thread
    pthread_t thread_id;
    engine_t *current_engine;
    int status;

    status = pthread_key_create (&engine_key, destructor);
    if (status != 0)
        err_abort (status, "Create key");
    status = workq_init (&workq, NO_OF_THREADS, engine_routine,proj_space_save,proj_space_load);
    if (status != 0)
        err_abort (status, "Init work queue");
    status = pthread_create (&thread_id, NULL, control_thread_routine, NULL);
    if (status != 0)
        err_abort (status, "Create thread");
    //so here we are now waiting for the control thread to end:
    status = pthread_join (thread_id, NULL);
    if (status != 0)
        err_abort (status, "Join thread");
    status = workq_destroy (&workq);
    if (status != 0)
        err_abort (status, "Destroy work queue");
    //output result:
    printf("Process complete, max k realised: %d\n",max_k);
    return 0;
}

