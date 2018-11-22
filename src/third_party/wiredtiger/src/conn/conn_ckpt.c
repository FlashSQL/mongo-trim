/*-
 * Copyright (c) 2014-2015 MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"

#if defined(TDN_TRIM5) || defined (TDN_TRIM5_2)
#include "mytrim.h"

extern TRIM_MAP* trimmap;
extern off_t *my_starts_tem, *my_ends_tem;
extern FILE* my_fp4;
extern size_t my_trim_freq_config; //how often trim will call

extern pthread_t trim_tid;
extern pthread_mutex_t trim_mutex;
extern pthread_cond_t trim_cond;
extern bool my_is_trim_running;
#endif //TDN_TRIM5

static int __ckpt_server_start(WT_CONNECTION_IMPL *);

/*
 * __ckpt_server_config --
 *	Parse and setup the checkpoint server options.
 */
static int
__ckpt_server_config(WT_SESSION_IMPL *session, const char **cfg, bool *startp)
{
	WT_CONFIG_ITEM cval;
	WT_CONNECTION_IMPL *conn;
	WT_DECL_ITEM(tmp);
	WT_DECL_RET;
	char *p;

	conn = S2C(session);

	/*
	 * The checkpoint configuration requires a wait time and/or a log
	 * size -- if one is not set, we're not running at all.
	 * Checkpoints based on log size also require logging be enabled.
	 */
	WT_RET(__wt_config_gets(session, cfg, "checkpoint.wait", &cval));
	conn->ckpt_usecs = (uint64_t)cval.val * WT_MILLION;

	WT_RET(__wt_config_gets(session, cfg, "checkpoint.log_size", &cval));
	conn->ckpt_logsize = (wt_off_t)cval.val;

	/* Checkpoints are incompatible with in-memory configuration */
	if (conn->ckpt_usecs != 0 || conn->ckpt_logsize != 0) {
		WT_RET(__wt_config_gets(session, cfg, "in_memory", &cval));
		if (cval.val != 0)
			WT_RET_MSG(session, EINVAL,
			    "In memory configuration incompatible with "
			    "checkpoints");
	}

	__wt_log_written_reset(session);
	if ((conn->ckpt_usecs == 0 && conn->ckpt_logsize == 0) ||
	    (conn->ckpt_logsize && conn->ckpt_usecs == 0 &&
	     !FLD_ISSET(conn->log_flags, WT_CONN_LOG_ENABLED))) {
		*startp = false;
		return (0);
	}
	*startp = true;

	/*
	 * The application can specify a checkpoint name, which we ignore if
	 * it's our default.
	 */
	WT_RET(__wt_config_gets(session, cfg, "checkpoint.name", &cval));
	if (cval.len != 0 &&
	    !WT_STRING_MATCH(WT_CHECKPOINT, cval.str, cval.len)) {
		WT_RET(__wt_checkpoint_name_ok(session, cval.str, cval.len));

		WT_RET(__wt_scr_alloc(session, cval.len + 20, &tmp));
		WT_ERR(__wt_buf_fmt(
		    session, tmp, "name=%.*s", (int)cval.len, cval.str));
		WT_ERR(__wt_strdup(session, tmp->data, &p));

		__wt_free(session, conn->ckpt_config);
		conn->ckpt_config = p;
	}

err:	__wt_scr_free(session, &tmp);
	return (ret);
}

#if defined (TDN_TRIM5) || defined (TDN_TRIM5_2)
/*
 * quicksort based on x array, move associate element in y arrays
 * x, y have the same length
 * */
static void quicksort(off_t* x, off_t* y,  int32_t first, int32_t last){
	int32_t pivot, i, j;
	off_t temp;

	if(first < last) {
		pivot = first;
		i = first;
		j = last;
		
		while(i < j){
			while(x[i] <= x[pivot] && i < last)
				i++;
			while(x[j] > x[pivot])
				j--;
			if(i < j){
				//swap in x
				temp = x[i];
				x[i] = x[j];
				x[j] = temp;
				//swap in y
				temp = y[i];
				y[i] = y[j];
				y[j] = temp;
			}
		}

		temp = x[pivot];
		x[pivot] = x[j];
		x[j] = temp;

		temp = y[pivot];
		y[pivot] = y[j];
		y[j] = temp;

		quicksort(x, y, first, j - 1);
		quicksort(x, y, j + 1, last);
	}
}
/*
 *1: sort the ranges in order
  2: merge overlap ranges
  3: call TRIM command for merged ranges
 * */
void __trim_sort_merge(TRIM_OBJ* obj, int32_t size){
	off_t cur_start, cur_end;
	struct fstrim_range range;

	int32_t i, myret;

	//copy offsets 
	memcpy(my_starts_tem, obj->starts, size * sizeof(off_t));
	memcpy(my_ends_tem, obj->ends, size * sizeof(off_t));
	//sort
	quicksort(my_starts_tem, my_ends_tem, 0, size - 1);
	//scan through ranges, try join overlap range then call trim
	cur_start = my_starts_tem[0];
	cur_end = my_ends_tem[0];

	//loop call TRIM command for each range
	for(i = 1; i < size; i++){
		if(cur_end < my_starts_tem[i]) {
			//non-overlap, trim the current range
			if ((cur_end - cur_start) <= 0){
				fprintf(my_fp4, "logical error cur_end <= cur_start\n");
				//skip trimming
			}
			else {
				range.len = cur_end - cur_start;
				range.start = cur_start;
				range.minlen = 4096; //at least 4KB
				myret = ioctl(obj->fd, FITRIM, &range);
				if(myret < 0){
					perror("ioctl");
					fprintf(my_fp4, 
							"call trim error ret %d errno %s range.start %llu range.len %llu range.minlen %llu\n",
							myret, strerror(errno), range.start, range.len, range.minlen);
				}	
			}
			cur_start = my_starts_tem[i];
			cur_end = my_ends_tem[i];
		}	
		else {
			//overlap case, join two range, keep the cur_start, 
			//extend the cur_end
			if(cur_end <= my_ends_tem[i]){
				cur_end = my_ends_tem[i]; //extend
			}
			else {
				//kept the same
			}
		}
	} //end for
}
/*
 *A simple trim command apporach
 For a given ranges, call TRIM command for each range
 Pros: eliminate overhead of memcpy, sort, merge ranges
 cons: more ioctl() calls 
 * */
static void __trim_simple(TRIM_OBJ* obj, int32_t size) {
	
	struct fstrim_range range;

	int32_t i, myret;
	/*
	 *Since we use the single shared my_starts_tem and my_ends_tem, when the 
	 thread is call too quickly, the previous values may be overwritten by the
	 later call => don't use the tem buffer anymore 
	 * */	
	//memcpy(my_starts_tem, obj->starts, size * sizeof(off_t));
	//memcpy(my_ends_tem, obj->ends, size * sizeof(off_t));
	for(i = 0; i < size; i++){
		range.start = obj->starts[i];
		range.len = (obj->ends[i] - obj->starts[i]);
		range.minlen = 4096;
		myret = ioctl(obj->fd, FITRIM, &range);
		if(myret < 0){
			perror("ioctl");
			fprintf(my_fp4, 
					"call trim error ret %d errno %s range.start %llu range.len %llu range.minlen %llu\n",
					myret, strerror(errno), range.start, range.len, range.minlen);
		}	
	}//end for
}

/* 
 * The trim handle thread
 * Triggered by trim_cond
 * Call trim for multiple ranges
 * fd: file description that trim will occur on
 * starts: array start offset
 * ends: array end offset
 * size: size of arrays, starts and ends have the same size
 * arg: fd that trim will occur on
 * */
static WT_THREAD_RET 
__trim_ranges(void* arg) {
	
	//off_t cur_start, cur_end;
	//struct fstrim_range range;

	//int32_t i, myret;
	int32_t size;
	TRIM_OBJ* obj;

	while (trimmap->oid == TRIM_INDEX_NOT_SET && my_is_trim_running) {
		//wait for pthread_cond_signal
		pthread_cond_wait(&trim_cond, &trim_mutex);
		// wait ...
	
		//when the process reach this line, trimmap->oid should != TRIM_INDEX_NOT_SET
		printf("TRIM thread is active, trimmap->oid=%d\n ", trimmap->oid);	
		//check again
		if(trimmap->oid < 0) continue;
		
		obj = trimmap->data[trimmap->oid];

		//obj->size may changed during this processs, take a snapshot here
	    size = obj->size;	
		if (obj->size == 0) continue;

		//signaled by other, now handle trim
	
		printf("inside TRIM handle thread, call __trim_ranges, size = %d, oid=%d\n", size, trimmap->oid);
		fprintf(my_fp4, "inside TRIM handle thread, call __trim_ranges, size = %d, oid=%d\n", size, trimmap->oid);
		//Choose between two options: 
		//1: trim with sort and merge 
		//2: simple trim ranges		
		
		__trim_sort_merge(obj, size);	
		//__trim_simple(obj, size);
		//reset

		trimmap->oid = TRIM_INDEX_NOT_SET;
		obj->size = 0; //reset

		//For large enough time interval, sleep some minutes to avoid unexpected thread bug
		//NOTICE: for multiple files, when the thread is sleeping, there may have another trigger
		//that make the trimmap->oid != TRIM_INDEX_NOT_SET => end WHILE
		//so we don't sleep anymore 
		if(size >= 10000){
			//sleep(500);
			//sleep(10);
		}

	} //end while
	pthread_exit(NULL);
	return (WT_THREAD_RET_VALUE);
}	
#endif // TDN_TRIM5

/*
 * __ckpt_server --
 *	The checkpoint server thread.
 */
static WT_THREAD_RET
__ckpt_server(void *arg)
{
	WT_CONNECTION_IMPL *conn;
	WT_DECL_RET;
	WT_SESSION *wt_session;
	WT_SESSION_IMPL *session;

	session = arg;
	conn = S2C(session);
	wt_session = (WT_SESSION *)session;

	while (F_ISSET(conn, WT_CONN_SERVER_RUN) &&
	    F_ISSET(conn, WT_CONN_SERVER_CHECKPOINT)) {
		/*
		 * Wait...
		 * NOTE: If the user only configured logsize, then usecs
		 * will be 0 and this wait won't return until signalled.
		 */
		WT_ERR(
		    __wt_cond_wait(session, conn->ckpt_cond, conn->ckpt_usecs));

		/* Checkpoint the database. */
		WT_ERR(wt_session->checkpoint(wt_session, conn->ckpt_config));

		/* Reset. */
		if (conn->ckpt_logsize) {
			__wt_log_written_reset(session);
			conn->ckpt_signalled = 0;

			/*
			 * In case we crossed the log limit during the
			 * checkpoint and the condition variable was already
			 * signalled, do a tiny wait to clear it so we don't do
			 * another checkpoint immediately.
			 */
			WT_ERR(__wt_cond_wait(session, conn->ckpt_cond, 1));
		}
	}

	if (0) {
err:		WT_PANIC_MSG(session, ret, "checkpoint server error");
	}
	return (WT_THREAD_RET_VALUE);
}

/*
 * __ckpt_server_start --
 *	Start the checkpoint server thread.
 */
static int
__ckpt_server_start(WT_CONNECTION_IMPL *conn)
{
	WT_SESSION_IMPL *session;
	uint32_t session_flags;

	/* Nothing to do if the server is already running. */
	if (conn->ckpt_session != NULL)
		return (0);

	F_SET(conn, WT_CONN_SERVER_CHECKPOINT);

	/*
	 * The checkpoint server gets its own session.
	 *
	 * Checkpoint does enough I/O it may be called upon to perform slow
	 * operations for the block manager.
	 */
	session_flags = WT_SESSION_CAN_WAIT;
	WT_RET(__wt_open_internal_session(conn,
	    "checkpoint-server", true, session_flags, &conn->ckpt_session));
	session = conn->ckpt_session;

	WT_RET(__wt_cond_alloc(
	    session, "checkpoint server", false, &conn->ckpt_cond));

	/*
	 * Start the thread.
	 */
	WT_RET(__wt_thread_create(
	    session, &conn->ckpt_tid, __ckpt_server, session));
	conn->ckpt_tid_set = true;

	return (0);
}

/*
 * __wt_checkpoint_server_create --
 *	Configure and start the checkpoint server.
 */
int
__wt_checkpoint_server_create(WT_SESSION_IMPL *session, const char *cfg[])
{
	WT_CONNECTION_IMPL *conn;
	bool start;

	conn = S2C(session);
	start = false;

	/* If there is already a server running, shut it down. */
	if (conn->ckpt_session != NULL)
		WT_RET(__wt_checkpoint_server_destroy(session));

	WT_RET(__ckpt_server_config(session, cfg, &start));
	if (start)
		WT_RET(__ckpt_server_start(conn));

	return (0);
}

/*
 * __wt_checkpoint_server_destroy --
 *	Destroy the checkpoint server thread.
 */
int
__wt_checkpoint_server_destroy(WT_SESSION_IMPL *session)
{
	WT_CONNECTION_IMPL *conn;
	WT_DECL_RET;
	WT_SESSION *wt_session;

	conn = S2C(session);

	F_CLR(conn, WT_CONN_SERVER_CHECKPOINT);
	if (conn->ckpt_tid_set) {
		WT_TRET(__wt_cond_signal(session, conn->ckpt_cond));
		WT_TRET(__wt_thread_join(session, conn->ckpt_tid));
		conn->ckpt_tid_set = false;
	}
	WT_TRET(__wt_cond_destroy(session, &conn->ckpt_cond));

	__wt_free(session, conn->ckpt_config);

	/* Close the server thread's session. */
	if (conn->ckpt_session != NULL) {
		wt_session = &conn->ckpt_session->iface;
		WT_TRET(wt_session->close(wt_session, NULL));
	}

	/*
	 * Ensure checkpoint settings are cleared - so that reconfigure doesn't
	 * get confused.
	 */
	conn->ckpt_session = NULL;
	conn->ckpt_tid_set = false;
	conn->ckpt_cond = NULL;
	conn->ckpt_config = NULL;
	conn->ckpt_usecs = 0;

	return (ret);
}

/*
 * __wt_checkpoint_signal --
 *	Signal the checkpoint thread if sufficient log has been written.
 *	Return 1 if this signals the checkpoint thread, 0 otherwise.
 */
int
__wt_checkpoint_signal(WT_SESSION_IMPL *session, wt_off_t logsize)
{
	WT_CONNECTION_IMPL *conn;

	conn = S2C(session);
	WT_ASSERT(session, WT_CKPT_LOGSIZE(conn));
	if (logsize >= conn->ckpt_logsize && !conn->ckpt_signalled) {
		WT_RET(__wt_cond_signal(session, conn->ckpt_cond));
		conn->ckpt_signalled = 1;
	}
	return (0);
}
