/*
 *Author: tdnguyen
 Simple map table implementation for TRIM command optimization
 * */
#ifndef __MYTRIM_H__
#define __MYTRIM_H__

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdint.h> //for uint32_t

#include <sys/ioctl.h> //for ioctl call
#include <linux/fs.h> //for fstrim_range
#include <string.h>
#include <errno.h>
#define TRIM_MAX_FILE 20
#define TRIM_MAX_FILE_NAME_LENGTH 256

//tuning those parameters =============================================
//#define TRIM_INIT_THRESHOLD 1600000
//#define TRIM_MAX_THRESHOLD 16000000
//#define TRIM_INIT_THRESHOLD 32000
#define TRIM_INIT_THRESHOLD 32000
#define TRIM_MAX_THRESHOLD (TRIM_INIT_THRESHOLD*5)
//used for update max_size based on count,
//f = how many times call TRIM in a checkpoint
//f = 1 / TRIM_THRESHOLD_PCT
#define TRIM_THRESHOLD_PCT 0.65 // used for update max_size based on count, f = how many times call TRIM in a checkpoint

// end tuning parameters===============================================

#define TRIM_MAX_RANGE TRIM_MAX_THRESHOLD
//#define TRIM_MAX_RANGE 10000000
#define TRIM_INDEX_NOT_SET -1


/*
 *One trim object for one file
 Trim object can store number of ranges then call TRIM command in batch
 "count" field used for counting number of events (replacement, discard) occurs during a checkpoint
 at checkpoint time update the max_size deppend on "count" field
 "size" and "count" are not the same, both of them increase 1 when a new range is added
 "size" is reset to 0 when cal TRIM command
 "count" is reset to 0 when checkpoint is called
 * */
typedef struct __trim_object {
	int fd; //file 
	off_t* starts;
	off_t* ends;	
	uint32_t size; //current number of ranges
	uint32_t count;//count number of event (replacement, discard) per checkpoint
	uint32_t max_size; //threshold that is number of ranges save up, if the current number of ranges is over this value, the TRIM command will call
} __trim_object;
typedef struct __trim_object TRIM_OBJ;

typedef struct __trim_map {
	TRIM_OBJ** data;	
	uint32_t size; //current number of object 
	int oid; //oversize index 
} __trim_map;
typedef __trim_map TRIM_MAP;

static inline TRIM_MAP* trimmap_new();
static inline TRIM_OBJ* __trimobj_new(int fd, uint32_t max);
static inline void __trimobj_free(TRIM_OBJ* o);
static inline void trimmap_free(TRIM_MAP* m);
static inline void trimmap_add(TRIM_MAP* m, int fd, uint32_t obj_max_size);
static inline int trimmap_find(TRIM_MAP* m, int fd);
static inline uint32_t trimobj_add_range(TRIM_OBJ* obj, off_t start, off_t end);



//MSSD_MAP* mssdmap_new() {
static inline TRIM_MAP* trimmap_new() {
	TRIM_MAP* m = (TRIM_MAP*) malloc(sizeof(TRIM_MAP));
	if(!m) goto err;

	m->data = (TRIM_OBJ**) calloc(TRIM_MAX_FILE, sizeof(TRIM_OBJ*));
	if(!m->data) goto err;
	
	m->size = 0;
	m->oid = TRIM_INDEX_NOT_SET;
	return m;

err:
	if (m)
		trimmap_free(m);
	return NULL;
}
static inline TRIM_OBJ* __trimobj_new(int fd, uint32_t max){
	TRIM_OBJ* o = (TRIM_OBJ*) malloc(sizeof(TRIM_OBJ));
	if(!o) goto err;

	o->starts = (off_t*) calloc(TRIM_MAX_RANGE, sizeof(off_t));
	o->ends = (off_t*) calloc(TRIM_MAX_RANGE, sizeof(off_t));
	if(!o->starts || !o->ends) goto err;
	
	o->fd = fd;
	o->size = 0;
	o->count = 0;
	o->max_size = max;
	return o;
err:
	if (o)
		__trimobj_free(o);
	return NULL;
		
}
static inline void __trimobj_free(TRIM_OBJ* o){
	free(o->starts);
	free(o->ends);
	free(o);
}
static inline void trimmap_free(TRIM_MAP* m) {
	uint32_t i;
	if (m->size > 0) {
		for (i = 0; i < m->size; i++) {
			__trimobj_free(m->data[i]);
		}
	}
	m->size = 0;

	free(m->data);
	free(m);
}
/* Main function use for TRIM command 
 * * */
//int mssdmap_get_or_append(MSSD_MAP* m, const char* key, const off_t val, off_t* retval) {
static inline void trimmap_add(TRIM_MAP* m, int fd, uint32_t obj_max_size) {
	uint32_t i;
	if(m->size < 0 || (m->size > TRIM_MAX_FILE - 1)) return;

	for(i = 0; i < m->size; i++){
		if(m->data[i]->fd == fd){
			//already exist
			return;
		}
	}
	m->data[m->size] = __trimobj_new(fd, obj_max_size);
	m->size++;
	printf("in trimmap_add m->size = %d, fd=%d \n", m->size, fd);
	return;
}

/* find key and return index in the array
 * Just simple scan whole items.
 * The number of files are expected small
 *return -1 if key is not exist
 * */
static inline int trimmap_find(TRIM_MAP* m, int fd){
	uint32_t i;
	for (i = 0; i < m->size; i++){
		if (m->data[i]->fd == fd)
			return i;
	}
	return -1;
}
/*
 *add a range to an object
 return the current size of obj
 * */
static inline uint32_t trimobj_add_range(TRIM_OBJ* obj, off_t start, off_t end) {
	if(obj->size < 0 || obj->size > TRIM_MAX_RANGE - 10){
		printf("trimobj_add_range, size is wrong, obj->size is %d\n",obj->size);
		obj->count++;
		return obj->size;
	}
	obj->starts[obj->size] = start;
	obj->ends[obj->size] = end;
	obj->size++;
	
	obj->count++;
	return obj->size;
}
/*
 * used by dynamic TRIM command approach (TDN_TRIM5)
 *update max_size for all obj in trimmap
 call this function when checkpoint
 * */
static inline void trimmap_update_max_size(TRIM_MAP* m){
	uint32_t i, size;
	TRIM_OBJ* obj;
	uint32_t tem = 0;

	size = m->size;
	printf("inside trimmap_update_max_size, size=%d\n", size);
	
	for (i =0; i < size; i++){
		//if ( i > 10) return;
		obj = m->data[i];
		
		printf("==> index %d size= %d old max_size: %d events within this ckpt: %d ", i, size, obj->max_size, obj->count);
		if(obj->count > obj->max_size){
			tem = (uint32_t) (obj->count * TRIM_THRESHOLD_PCT);	
			if(tem > TRIM_MAX_THRESHOLD)
				tem = TRIM_MAX_THRESHOLD;
			else if(tem < TRIM_INIT_THRESHOLD)
				tem = TRIM_INIT_THRESHOLD;
			obj->max_size = tem;
		}
		
		obj->size = 0;
		obj->count = 1;
		printf(" new max_size %d\n", obj->max_size);
	}
}
#endif //__MSSD_H__
