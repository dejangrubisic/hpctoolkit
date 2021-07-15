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

//***************************************************************************
//
// File:
//   nvml-api.c
//
// Purpose:
//   wrapper around NVIDIA NVML layer
//
//***************************************************************************


//*****************************************************************************
// system include files
//*****************************************************************************

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>    // memset



//*****************************************************************************
// local include files
//*****************************************************************************

#include <hpcrun/sample-sources/libdl.h>
#include <hpcrun/messages/messages.h>

#include "nvml-api.h"



//*****************************************************************************
// macros
//*****************************************************************************

#define NVML_FN_NAME(f) DYN_FN_NAME(f)

#define NVML_FN(fn, args) \
  static nvmlReturn_t (*NVML_FN_NAME(fn)) args

#define NVML_ERROR_STRING_FN(fn, args) \
  static const DECLDIR char * (*NVML_FN_NAME(fn)) args

#define HPCRUN_NVML_API_CALL(fn, args)                              \
{                                                                   \
  nvmlReturn_t error_result = NVML_FN_NAME(fn) args;		    \
  if (error_result != NVML_SUCCESS) {				    \
    ETMSG(NVML, "nvml api %s returned %d", #fn,                     \
          (int) error_result);                                      \
    exit(-1);							    \
  }								    \
}


#define HPCRUN_NVML_CALL(fn, args)                          \
{                                                                   \
  nvmlReturn_t error_result = NVML_FN_NAME(fn) args;		    \
  if (error_result != NVML_SUCCESS) {				    \
    ETMSG(NVML, "nvml call %s returned %d", #fn,                 \
          (int) error_result);                                      \
    exit(-1);							    \
  }								    \
}


//----------------------------------------------------------------------
// device capability
//----------------------------------------------------------------------

#define COMPUTE_MAJOR_TURING 	7
#define COMPUTE_MINOR_TURING 	5

#define DEVICE_IS_TURING(major, minor)      \
  ((major == COMPUTE_MAJOR_TURING) && (minor == COMPUTE_MINOR_TURING))


//----------------------------------------------------------------------
// runtime version
//----------------------------------------------------------------------

#define RUNTIME_MAJOR_VERSION(rt_version) (rt_version / 1000) 
#define RUNTIME_MINOR_VERSION(rt_version) (rt_version % 10) 


/*
Return a number with a specific meaning. This number needs to be interpreted and handled appropriately.
*/


static int 
getNVMLError(nvmlReturn_t resultToCheck)
{
	if (resultToCheck == NVML_ERROR_UNINITIALIZED)
		return 1;
	if (resultToCheck == NVML_ERROR_INVALID_ARGUMENT)
		return 2;
	if (resultToCheck == NVML_ERROR_NOT_SUPPORTED)
		return 3;
	if (resultToCheck == NVML_ERROR_NO_PERMISSION)
		return 4;
	if (resultToCheck == NVML_ERROR_ALREADY_INITIALIZED)
		return 5;
	if (resultToCheck == NVML_ERROR_NOT_FOUND)
		return 6;
	if (resultToCheck == NVML_ERROR_INSUFFICIENT_SIZE)
		return 7;
	if (resultToCheck == NVML_ERROR_INSUFFICIENT_POWER)
		return 8;
	if (resultToCheck == NVML_ERROR_DRIVER_NOT_LOADED)
		return 9;
	if (resultToCheck == NVML_ERROR_TIMEOUT)
		return 10;
	if (resultToCheck == NVML_ERROR_IRQ_ISSUE)
		return 11;
	if (resultToCheck == NVML_ERROR_LIBRARY_NOT_FOUND)
		return 12;
	if (resultToCheck == NVML_ERROR_FUNCTION_NOT_FOUND)
		return 13;
	if (resultToCheck == NVML_ERROR_CORRUPTED_INFOROM)
		return 14;
	if (resultToCheck == NVML_ERROR_GPU_IS_LOST)
		return 15;
	if (resultToCheck == NVML_ERROR_UNKNOWN)
		return 16;

	return 0;
}

//*****************************************************************************
// BIND NVML Library
//*****************************************************************************

#ifndef HPCRUN_STATIC_LINK
NVML_FN
(
 nvmlInit,
 (
  void
 )
);


NVML_FN
(
 nvmlShutdown,
 (
  void
 )
);


NVML_ERROR_STRING_FN
(
 nvmlErrorString,
 (
  nvmlReturn_t result
 )
);


NVML_FN
(
 nvmlSystemGetDriverVersion,
 ( 
  char *version, 
  unsigned int length
 )
);


NVML_FN
(
 nvmlSystemGetNVMLVersion,
 (
  char *version, 
  unsigned int length
 )
);


NVML_FN
(
 nvmlDeviceGetCount,
 (
  unsigned int *deviceCount
 )
);


NVML_FN
(
 nvmlDeviceGetHandleByIndex,
 (
  unsigned int index, 
  nvmlDevice_t *device
 )
);


NVML_FN
(
 nvmlDeviceGetPowerUsage,
 (
  nvmlDevice_t device, 
  unsigned int *power
 )
);


NVML_FN
(
 nvmlDeviceGetSamples,
 (
  nvmlDevice_t device, 
  nvmlSamplingType_t type, 
  unsigned long long lastSeenTimeStamp, 
  nvmlValueType_t *sampleValType, 
  unsigned int *sampleCount, 
  nvmlSample_t *samples
 )
);
#endif



//******************************************************************************
// private operations
//******************************************************************************

int
nvml_bind
(
  void
)
{
#ifndef HPCRUN_STATIC_LINK
  // dynamic libraries only availabile in non-static case
  CHK_DLOPEN(nvml, "libnvidia-ml.so", RTLD_NOW | RTLD_GLOBAL);

  CHK_DLSYM(nvml, nvmlInit);
  CHK_DLSYM(nvml, nvmlShutdown);
  CHK_DLSYM(nvml, nvmlErrorString);
  CHK_DLSYM(nvml, nvmlSystemGetDriverVersion);
  CHK_DLSYM(nvml, nvmlSystemGetNVMLVersion);
  CHK_DLSYM(nvml, nvmlDeviceGetCount);
  CHK_DLSYM(nvml, nvmlDeviceGetHandleByIndex);
  CHK_DLSYM(nvml, nvmlDeviceGetPowerUsage);
  CHK_DLSYM(nvml, nvmlDeviceGetSamples);
  

  return DYNAMIC_BINDING_STATUS_OK;
#else
  return DYNAMIC_BINDING_STATUS_ERROR;
#endif // ! HPCRUN_STATIC_LINK
}




//******************************************************************************
// interface operations
//******************************************************************************

