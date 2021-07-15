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
// Copyright ((c)) 2002-2021, Rice University
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

//
// attribute GPU performance metrics
//

#include <time.h>
#include <sys/time.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>

//******************************************************************************
// local includes
//******************************************************************************

// #include <hpcrun/cct2metrics.h>
#include <hpcrun/memory/hpcrun-malloc.h>
#include <hpcrun/utilities/tokenize.h>

#include "gpu-metrics-trace.h"

#include "simple_oo.h"
#include "sample_source_obj.h"
#include "common.h"
#include "display.h"

#include "libdl.h"

// #include <hpcrun/safe-sampling.h>
// #include <hpcrun/thread_data.h>

// #include "gpu-activity.h"
// #include "gpu-metrics.h"
// #include "gpu-monitoring.h"



//*****************************************************************************
// macros
//*****************************************************************************

#define NVML_POWER "trace_metric=power"
#define MAX_SAMPLES 100
#define POLL_TIME_US 10000



//*****************************************************************************
// private operations
//*****************************************************************************

typedef void *(*pthread_start_routine_t)(void *);

static bool pollThreadStatus = false;
pthread_t powerPollThread;


static unsigned int deviceCount = 0;
static trace_metrics_node_t *metrics_list;
static nvml_device_t *gpu_nvml_devices;
nvmlEnableState_t pmmode;
// static trace_metrics_t trace_metric = POWER;




/*
Start power measurement by spawning a pthread that polls the GPU.
Function needs to be modified as per usage to handle errors as seen fit.
*/


/*
Poll the GPU using nvml APIs.
*/
#if 0
void *powerPollingFunc(void *ptr)
{
	unsigned long long prev_time = 0, now_time = 0;   
	struct timeval tval_start = {0, 0}, tval_new = {0, 0};

	unsigned int powerLevel = 0;
	unsigned int temp = 0;
	// FILE *fp = fopen("Power_data.txt", "w+");

	// Get the power management mode of the GPU.
	nvmlResult = nvmlDeviceGetPowerManagementMode(gpu_handle, &pmmode);

	// The following function may be utilized to handle errors as needed.
	getNVMLError(nvmlResult);

	// Check if power management mode is enabled.
	if (pmmode != NVML_FEATURE_ENABLED)
	{
		printf("power management mode is disabled failed \n"); exit(-1);
	}


	gettimeofday (&tval_start, NULL);
	prev_time = tval_start.tv_sec * 1000000L + tval_start.tv_usec;

	printf("START_TIME = %lu us ================================== \n", prev_time);



	while (pollThreadStatus)
	{
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);
		
		// Get the power usage in milliWatts.
		nvmlResult = nvmlDeviceGetPowerUsage(gpu_handle, &powerLevel);
		if (nvmlResult != NVML_SUCCESS){
			printf("nvmlDeviceGetPowerUsage failed \n"); exit(-1);
		}

		// nvmlResult = nvmlDeviceGetTemperature(gpu_handle, NVML_TEMPERATURE_GPU, &temp);
		// if (nvmlResult != NVML_SUCCESS){
		// 	printf("nvmlDeviceGetTemperature failed \n"); exit(-1);
		// }
		
		
		gettimeofday (&tval_new, NULL);
		now_time = tval_new.tv_sec * 1000000L + tval_new.tv_usec;

		printf("%lu us ", now_time - prev_time);
		// The output file stores power in Watts.
		printf("Power = %.3lf Watts\n", (powerLevel)/1000.0);
		// printf("Temp = %u C\n", temp);
		
		// usleep(POLL_TIME_US / 100);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
	}
	
	gettimeofday (&tval_new, NULL);
	printf("END_TIME = %lu us ================================== \n", tval_new.tv_sec * 1000000L + tval_new.tv_usec);

	// fclose(fp);
	pthread_exit(0);
}
#else

static void *
powerPollingFunc
(
  void *ptr
)
{  
	nvmlReturn_t nvmlResult;
	unsigned long long prev_time = 0;
	nvmlSample_t *buffer = (nvmlSample_t*) malloc(MAX_SAMPLES * sizeof(nvmlSample_t));
	unsigned int samplesCount;
	nvmlValueType_t sampleValType = NVML_VALUE_TYPE_UNSIGNED_INT;

	struct timeval tval_start = {0, 0}, tval_new = {0, 0};



	gettimeofday (&tval_start, NULL);
	printf("START_TIME = %lu us ================================== \n", tval_start.tv_sec * 1000000L + tval_start.tv_usec);

	prev_time = tval_start.tv_sec * 1000000L + tval_start.tv_usec;

	while (pollThreadStatus){
		// pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);

    for(int i = 0; i < deviceCount; i++){
      
      nvmlResult = nvmlDeviceGetSamples (gpu_nvml_devices[i].handle, NVML_GPU_UTILIZATION_SAMPLES, prev_time, &sampleValType, &samplesCount, NULL);
      if (nvmlResult != NVML_SUCCESS){
        printf("nvmlDeviceGetSamples failed: %d %s\n", nvmlResult, nvmlErrorString(nvmlResult)); exit(-1);
      }
      // printf("FIRST samplesCount = %d\n", samplesCount);

      if (samplesCount > sizeof(buffer) / sizeof(nvmlSample_t)){
        buffer = (nvmlSample_t*) realloc(buffer, samplesCount * sizeof(nvmlSample_t));
      }
      
      nvmlResult = nvmlDeviceGetSamples (gpu_nvml_devices[i].handle, NVML_TOTAL_POWER_SAMPLES, prev_time, &sampleValType, &samplesCount, buffer);
      gettimeofday (&tval_new, NULL);
      
      
      // printf("SECOND samplesCount = %d\n", samplesCount);
      for (int j = 0; i < samplesCount; j++){
        if (prev_time >= buffer[j].timeStamp && 0){
          printf("NOT ORDERED \n"); 
          // int a =1;
        }else{
          prev_time = buffer[j].timeStamp;
          printf("REALTIME = %lu us  \t", tval_new.tv_sec * 1000000L + tval_new.tv_usec);
          printf("%llu us  \t", buffer[j].timeStamp);
          // The output file stores power in Watts.
          printf(" | P = %f Watts \n", ( (unsigned int) buffer[j].sampleValue.uiVal / 1000.0));			
        }
                
      } // samplesCount

    } // deviceCount

    usleep(POLL_TIME_US);
      
      // pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
  }

	gettimeofday (&tval_new, NULL);
	printf("END_TIME = %lu us ================================== \n", tval_new.tv_sec * 1000000L + tval_new.tv_usec);


	free(buffer);
	pthread_exit(0);
}
#endif




static void
METHOD_FN(init){
  unsigned int i;
  nvmlDevice_t gpu_handle;
  nvmlReturn_t nvmlResult;
  char deviceNameStr[64];


#ifndef HPCRUN_STATIC_LINK
  if (nvml_bind() != DYNAMIC_BINDING_STATUS_OK) {
    EEMSG("hpcrun: unable to bind to NVIDIA NVML library %s\n", dlerror());
    monitor_real_exit(-1);
  }
#endif


	// Initialize nvml.
	nvmlResult = nvmlInit();
	if (NVML_SUCCESS != nvmlResult)
	{
		printf("NVML Init fail: %s\n", nvmlErrorString(nvmlResult));
		exit(0);
	}

	// Count the number of GPUs available.
	nvmlResult = nvmlDeviceGetCount(&deviceCount);
	if (NVML_SUCCESS != nvmlResult)
	{
		printf("Failed to query device count: %s\n", nvmlErrorString(nvmlResult));
		exit(0);
	}
	printf("GPU count = %d\n", deviceCount);

  gpu_nvml_devices = (nvml_device_t*) hpcrun_malloc_safe(sizeof(nvml_device_t) * deviceCount);


  // Initialize NVML devices
	for (i = 0; i < deviceCount; i++)
	{
		// Get the device ID.
		nvmlResult = nvmlDeviceGetHandleByIndex(i, &gpu_handle);
		if (NVML_SUCCESS != nvmlResult)
		{
			printf("Failed to get handle for device %d: %s\n", i, nvmlErrorString(nvmlResult));
			exit(0);
		}

		// Get the name of the device.
		nvmlResult = nvmlDeviceGetName(gpu_handle, deviceNameStr, sizeof(deviceNameStr)/sizeof(deviceNameStr[0]));
		if (NVML_SUCCESS != nvmlResult)
		{
			printf("Failed to get name of device %d: %s\n", i, nvmlErrorString(nvmlResult));
			exit(0);
		}
		printf("GPU_Name = %s\n", deviceNameStr);

    gpu_nvml_devices[i].index = i;
    gpu_nvml_devices[i].handle = gpu_handle;
    strcpy(gpu_nvml_devices[i].name, deviceNameStr);
	}

}


static void
METHOD_FN(thread_init)
{
}

static void
METHOD_FN(thread_init_action)
{ 
}


static void
METHOD_FN(start){
	pollThreadStatus = true;

	int iret = pthread_create(&powerPollThread, NULL, (pthread_start_routine_t) powerPollingFunc, NULL);
	if (iret)
	{
		fprintf(stderr,"Error - pthread_create() return code: %d\n",iret);
		exit(0);
	}
}


static void
METHOD_FN(thread_fini_action)
{
}


static void
METHOD_FN(stop){  
	pollThreadStatus = false;
	pthread_join(powerPollThread, NULL);
}


static void
METHOD_FN(shutdown){
  nvmlReturn_t nvmlResult;

	nvmlResult = nvmlShutdown();
	if (NVML_SUCCESS != nvmlResult)
	{
		printf("Failed to shut down NVML: %s\n", nvmlErrorString(nvmlResult));
		exit(0);
	}

  free(gpu_nvml_devices);
}


static bool
METHOD_FN(supports_event, const char *ev_str)
{
#ifndef HPCRUN_STATIC_LINK
  return hpcrun_ev_is(ev_str, NVML_POWER);
#else
  return false;
#endif
};



static void
METHOD_FN(process_event_list, int lush_metrics)
{
  // bind here
}



static void
METHOD_FN(finalize_event_list)
{
}


static void
METHOD_FN(gen_event_set, int lush_metrics)
{
}


static void
METHOD_FN(display_events)
{
  printf("METRIC_TRACE SUPPORTS: *********************\nPOWER\n\n");
}

/***************************************************************************
 * object
 ***************************************************************************/

#define ss_name gpu_metrics_trace
#define ss_cls SS_HARDWARE
#define ss_sort_order  70

#include "ss_obj.h"