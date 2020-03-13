// -*-Mode: C++;-*- // technically C99

// * BeginRiceCopyright *****************************************************
//
// --------------------------------------------------------------------------
// Part of HPCToolkit (hpctoolkit.org)
//
// Information about sources of support for research and development of
// HPCToolkit is at 'hpctoolkit.org' and in 'README.Acknowledgments'.
// --------------------------------------------------------------------------
//
// Copyright ((c)) 2002-2020, Rice University
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
//
// * Neither the name of Rice University (RICE) nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// This software is provided by RICE and contributors "as is" and any
// express or implied warranties, including, but not limited to, the
// implied warranties of merchantability and fitness for a particular
// purpose are disclaimed. In no event shall RICE or contributors be
// liable for any direct, indirect, incidental, special, exemplary, or
// consequential damages (including, but not limited to, procurement of
// substitute goods or services; loss of use, data, or profits; or
// business interruption) however caused and on any theory of liability,
// whether in contract, strict liability, or tort (including negligence
// or otherwise) arising in any way out of the use of this software, even
// if advised of the possibility of such damage.
//
// ******************************************************* EndRiceCopyright *

//******************************************************************************
// local includes
//******************************************************************************

#include <pthread.h>



//******************************************************************************
// libmonitor 
//******************************************************************************

#include <monitor.h>



//******************************************************************************
// local includes
//******************************************************************************

#include <lib/prof-lean/stdatomic.h>

#include <hpcrun/cct/cct.h>
#include <hpcrun/thread_data.h>
#include <hpcrun/threadmgr.h>
#include <hpcrun/trace.h>

#include "gpu-context-id-map.h"
#include "gpu-monitoring.h"
#include "gpu-trace-channel.h"
#include "gpu-trace-item.h"
#include "gpu-trace-channel-set.h"
#include "gpu-trace.h"

//******************************************************************************
// macros
//******************************************************************************

#define DEBUG 0

#include "gpu-print.h"



//******************************************************************************
// type declarations
//******************************************************************************

typedef struct gpu_trace_t {
  pthread_t thread;
  gpu_trace_channel_t *trace_channel;

  // This is number of stack in which this trace is stored
  unsigned int map_id;
} gpu_trace_t;



typedef void *(*pthread_start_routine_t)(void *);


//******************************************************************************
// local variables
//******************************************************************************

static _Atomic(bool) stop_trace_flag;

static atomic_ullong stream_counter;

static atomic_ullong stream_id;



static __thread uint64_t stream_start = 0;



//******************************************************************************
// private operations
//******************************************************************************

static void
stream_start_set
(
 uint64_t start_time
)
{
  if (!stream_start) stream_start = start_time;
}


static uint64_t
stream_start_get
(
 void
)
{
  return stream_start;
}


static gpu_trace_t *
gpu_trace_alloc
(
 void
)
{
  gpu_trace_t *trace = hpcrun_malloc_safe(sizeof(gpu_trace_t));
  trace->trace_channel = gpu_trace_channel_alloc();
  return trace;
}


static cct_node_t *
gpu_trace_cct_root
(
 thread_data_t* td
)
{
  return td->core_profile_trace_data.epoch->csdata.tree_root;
}


static cct_node_t *
gpu_trace_cct_no_activity
(
 thread_data_t* td
)
{
  cct_node_t *no_activity = 
    hpcrun_cct_bundle_get_no_activity_node(&(td->core_profile_trace_data.epoch->csdata));

  return no_activity;
}


static cct_node_t *
gpu_trace_cct_insert_context
(
 thread_data_t* td,
 cct_node_t *path
)
{
  cct_node_t *leaf =
          hpcrun_cct_insert_path_return_leaf(gpu_trace_cct_root(td), path);

  return leaf;
}


static uint64_t
gpu_trace_time
(
 uint64_t gpu_time
)
{
  // return time in ns
  uint64_t time = gpu_time; 

  return time;
}


static void
gpu_trace_stream_append
(
 thread_data_t* td,
 cct_node_t *leaf,
 uint64_t time
)
{
  hpcrun_trace_append_stream(&td->core_profile_trace_data, leaf, 0, 
			     td->prev_dLCA, time); 
}


static void
gpu_trace_first
(
 thread_data_t* td,
 cct_node_t *no_activity,
 uint64_t start
)
{
  static __thread bool first = true;

  if (first) {
    first = false;
    gpu_trace_stream_append(td, no_activity, start - 1);
  }
}


static uint64_t
gpu_trace_start_adjust
(
 uint64_t start,
 uint64_t end
)
{
  static __thread uint64_t last_end = 0;

  if (start < last_end) {
    // If we have a hardware measurement error (Power9),
    // set the offset as the end of the last activity
    start = last_end + 1;
  }

  last_end = end;

  return start;
}


void
consume_one_trace_item
(
 thread_data_t* td,
 cct_node_t *call_path,
 uint64_t start_time,
 uint64_t end_time
)
{

  cct_node_t *leaf = gpu_trace_cct_insert_context(td, call_path);

  cct_node_t *no_activity = gpu_trace_cct_no_activity(td);

  uint64_t start = gpu_trace_time(start_time);
  uint64_t end   = gpu_trace_time(end_time);

  stream_start_set(start_time);

  start = gpu_trace_start_adjust(start, end);

  int frequency = gpu_monitoring_trace_sample_frequency_get();

  bool append = false;

  if (frequency != -1) {
    uint64_t cur_start = start_time;
    uint64_t cur_end = end_time;
    uint64_t intervals = (cur_start - stream_start_get() - 1) / frequency + 1;
    uint64_t pivot = intervals * frequency + stream_start;

    if (pivot <= cur_end && pivot >= cur_start) {
      // only trace when the pivot is within the range
      PRINT("pivot %" PRIu64 " not in <%" PRIu64 ", %" PRIu64 
	    "> with intervals %" PRIu64 ", frequency %" PRIu64 "\n",
        pivot, cur_start, cur_end, intervals, frequency);
      append = true;
    }
  } else {
    append = true;
  }

  if (append) {
    gpu_trace_first(td, no_activity, start);
    
    gpu_trace_stream_append(td, leaf, start);
    
    gpu_trace_stream_append(td, no_activity, end + 1);
    
    PRINT("%p Append trace activity [%lu, %lu]\n", td, start, end);
  }
}



static void
gpu_trace_activities_process
(
  int channel_num
)
{
    gpu_trace_channel_set_consume(channel_num);
}


static void
gpu_trace_activities_await
(
 gpu_trace_t* thread_args
)
{
  gpu_trace_channel_await(thread_args->trace_channel);
}


static int
gpu_trace_stream_id
(
 void
)
{
  // FIXME: this is a bad way to compute a stream id 
  int id = 500 + atomic_fetch_add(&stream_id, 1);

  return id;
}


thread_data_t *
gpu_trace_stream_acquire
(
 void
)
{
  thread_data_t* td = NULL;

  int id = gpu_trace_stream_id();

  hpcrun_threadMgr_non_compact_data_get(id, NULL, &td);

  return td;
}


void
gpu_trace_stream_release
(
 gpu_trace_channel_t *channel
)
{

  thread_data_t *td = channel->td;

  hpcrun_write_profile_data(&td->core_profile_trace_data);
  hpcrun_trace_close(&td->core_profile_trace_data);

    atomic_fetch_add(&stream_counter, -1);
}



//******************************************************************************
// interface operations
//******************************************************************************

void 
gpu_trace_init
(
 void
)
{
  atomic_store(&stop_trace_flag, false);
  atomic_store(&stream_counter, 0);
  atomic_store(&stream_id, 0);
}


void *
gpu_trace_record
(
 gpu_trace_t *thread_args
)
{

  while (!atomic_load(&stop_trace_flag)) {
      //getting data from a trace channel
    gpu_trace_activities_process( thread_args->map_id );
    gpu_trace_activities_await(thread_args);
  }

    gpu_trace_channel_set_release(thread_args->map_id);

  return NULL;
}


void
gpu_trace_fini
(
 void *arg
)
{
  PRINT("gpu_trace_fini called\n");

  atomic_store(&stop_trace_flag, true);

  gpu_context_stream_map_signal_all();


  while (atomic_load(&stream_counter) != 0 );

}




void *
schedule_multi_threads
(
 gpu_trace_t *trace
)
{

    int streams_per_thread = 4;
    static int num_threads = -1;
    unsigned long long stream_counter_local = atomic_fetch_add(&stream_counter, 1);

    if(stream_counter_local % streams_per_thread == 0){
        num_threads++;
        pthread_create(&trace->thread, NULL, (pthread_start_routine_t) gpu_trace_record, trace);
    }

    // First insert stream to the channel -> gpu_trace_channel_stack[thread_num]
    trace->map_id = num_threads;
    gpu_trace_channel_set_insert( trace->trace_channel,  trace->map_id);

    return NULL;

}



gpu_trace_t *
gpu_trace_create
(
 void
)
{
  // Init variables
  gpu_trace_t *trace = gpu_trace_alloc();

  // Create a new thread for the stream without libmonitor watching
  monitor_disable_new_threads();

  schedule_multi_threads(trace);

  monitor_enable_new_threads();

  return trace;
}


void 
gpu_trace_produce
(
 gpu_trace_t *t,
 gpu_trace_item_t *ti
)
{
  gpu_trace_channel_produce(t->trace_channel, ti);
}


void 
gpu_trace_signal_consumer
(
 gpu_trace_t *t
)
{
  gpu_trace_channel_signal_consumer(t->trace_channel);
}
