
#include <stdlib.h>

#define INITIAL_CAPACITY 100

typedef struct CVector{
    int capacity;
    int total;
    void **items;
}CVector;

void CVector_init(CVector* cvp){
    // allocate mem for CVector structure with as many voidptrs for items as capacity

    if (cvp == NULL) return;

    cvp->capacity = INITIAL_CAPACITY;
    cvp->total = 0;
    cvp->items = malloc(sizeof(void *) * cvp->capacity);
}

static void CVector_resize(CVector* cvp, int capacity){
    // resize CVector structure to capacity
    // WARNING: copies every item
    if (cvp == NULL) return;

    void** items = realloc(cvp->items, sizeof(void *) * capacity);
    if (items){
        cvp->items = items;
        cvp->capacity = capacity;
    }
}

void CVector_append(CVector* cvp, void* item){
    // append item at the end if enough capacity
    // if insuficcient capacity resize to size*2
    if (cvp == NULL) return;


    if (cvp->capacity == cvp->total){
        CVector_resize(cvp, cvp->capacity * 2);
    }
    cvp->items[cvp->total++] = item;
}

int CVector_set(CVector* cvp, void* item, const int index){
    // place item at index, overwriting old item
    // return 0 if success and -1 if bad index
    if (cvp == NULL) return -1;


    if (index >= 0 && index < cvp->total){
        cvp->items[index] = item;
        return 0;
    }
    return -1;
}

void* CVector_get(CVector* cvp, const int index){
    // return item at index and NULL if bad index
    if (cvp == NULL) return NULL;


    if (index >= 0 && index < cvp->total){
        return cvp->items[index];
    }
    return NULL;
}

int CVector_delete(CVector* cvp, const int index){
    // move every item after index to its index-1
    // this overwrites item at given index
    // after this resize to half capacity if #items <= (capacity/4)
    // return -1 if bad index and 0 if success
    if (cvp == NULL) return-1;

    if (index < 0 || index >= cvp->total) return -1;

    for (int i = index; i < cvp->total; i++){
        cvp->items[i] = cvp->items[i + 1];
    }

    cvp->total--;

    if (cvp->total > 0 && cvp->total <= (cvp->capacity / 4)){
        CVector_resize(cvp, (cvp->capacity / 2));
    }

    return 0;
}

int CVector_length(CVector* cvp){
    if (cvp == NULL) return -1;
    return cvp->total;
}

void CVector_free(CVector* cvp){
    if (cvp == NULL) return;
    free(cvp->items);
}

