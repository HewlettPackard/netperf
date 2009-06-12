/* what follows is a somewhat stripped-down version of the sample
   implementation of UUID generation from RFC 4122.  */

/*
** Copyright (c) 1990- 1993, 1996 Open Software Foundation, Inc.
** Copyright (c) 1989 by Hewlett-Packard Company, Palo Alto, Ca. &
** Digital Equipment Corporation, Maynard, Mass.
** Copyright (c) 1998 Microsoft.
** To anyone who acknowledges that this file is provided "AS IS"
** without any express or implied warranty: permission to use, copy,
** modify, and distribute this file for any purpose is hereby
** granted without fee, provided that the above copyright notices and
** this notice appears in all source code copies, and that none of
** the names of Open Software Foundation, Inc., Hewlett-Packard
** Company, Microsoft, or Digital Equipment Corporation be used in
** advertising or publicity pertaining to distribution of the software
** without specific, written prior permission. Neither Open Software
** Foundation, Inc., Hewlett-Packard Company, Microsoft, nor Digital
** Equipment Corporation makes any representations about the
** suitability of this software for any purpose.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

/* set the following to the number of 100ns ticks of the actual
   resolution of your system's clock */
#define UUIDS_PER_TICK 1024

#ifdef WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/time.h>
#include <sys/sysinfo.h>
#endif

/* system dependent call to get the current system time. Returned as
   100ns ticks since UUID epoch, but resolution may be less than
   100ns. */

#ifdef WIN32
#define I64(C) C
#else
#define I64(C) C##LL
#endif

typedef uint64_t uuid_time_t;

typedef struct {
  char nodeID[6];
} uuid_node_t;

#undef uuid_t
typedef struct {
  uint32_t  time_low;
  uint16_t  time_mid;
  uint16_t  time_hi_and_version;
  uint8_t   clock_seq_hi_and_reserved;
  uint8_t   clock_seq_low;
  uint8_t   node[6];
} uuid_t;

/* some forward declarations.  kind of wimpy to do that but heck, we
   are all friends here right?  raj 20081024 */
static uint16_t true_random(void);



#ifdef WIN32

static void get_system_time(uuid_time_t *uuid_time)
{
  ULARGE_INTEGER time;

  /* NT keeps time in FILETIME format which is 100ns ticks since
     Jan 1, 1601. UUIDs use time in 100ns ticks since Oct 15, 1582.
     The difference is 17 Days in Oct + 30 (Nov) + 31 (Dec)
     + 18 years and 5 leap days. */
  GetSystemTimeAsFileTime((FILETIME *)&time);
  time.QuadPart +=

    (unsigned __int64) (1000*1000*10)       // seconds
    * (unsigned __int64) (60 * 60 * 24)       // days
    * (unsigned __int64) (17+30+31+365*18+5); // # of days
  *uuid_time = time.QuadPart;
}

/* Sample code, not for use in production; see RFC 1750 */
static void get_random_info(char seed[16])
{
  uint16_t myrand;
  int i;

  i = 0;
  do {
    myrand = true_random();
    seed[i++] = myrand & 0xff;
    seed[i++] = myrand >> 8;
  } while (i < 14);

}

#else

static void get_system_time(uuid_time_t *uuid_time)
{
  struct timeval tp;

  gettimeofday(&tp, (struct timezone *)0);

  /* Offset between UUID formatted times and Unix formatted times.
     UUID UTC base time is October 15, 1582.
     Unix base time is January 1, 1970.*/
  *uuid_time = ((uint64_t)tp.tv_sec * 10000000)
    + ((uint64_t)tp.tv_usec * 10)
    + I64(0x01B21DD213814000);
}

/* Sample code, not for use in production; see RFC 1750 */
static void get_random_info(char seed[16])
{
  FILE *fp;
  uint16_t myrand;
  int i;

  /* we aren't all that picky, and we would rather not block so we
     will use urandom */
  fp = fopen("/dev/urandom","rb");

  if (NULL != fp) {
    fread(seed,sizeof(seed),1,fp);
    return;
  }

  /* ok, now what? */

  i = 0;
  do {
    myrand = true_random();
    seed[i++] = myrand & 0xff;
    seed[i++] = myrand >> 8;
  } while (i < 14);

}

#endif


/* true_random -- generate a crypto-quality random number.
**This sample doesn't do that.** */
static uint16_t true_random(void)
{
  static int inited = 0;
  uuid_time_t time_now;

  if (!inited) {
    get_system_time(&time_now);
    time_now = time_now / UUIDS_PER_TICK;
    srand((unsigned int)
	  (((time_now >> 32) ^ time_now) & 0xffffffff));
    inited = 1;
  }

  return rand();
}

/* puid -- print a UUID */
void puid(uuid_t u)
{
  int i;

  printf("%8.8x-%4.4x-%4.4x-%2.2x%2.2x-", u.time_low, u.time_mid,
	 u.time_hi_and_version, u.clock_seq_hi_and_reserved,
	 u.clock_seq_low);
  for (i = 0; i < 6; i++)
    printf("%2.2x", u.node[i]);
  printf("\n");
}

/* snpuid -- print a UUID in the supplied buffer */
void snpuid(char *str, size_t size, uuid_t u) {
  int i;
  char *tmp = str;

  if (size < 38) {
    snprintf(tmp,size,"%s","uuid string too small");
    return;
  }

  /* perhaps this is a trifle optimistic but what the heck */
  sprintf(tmp,
	  "%8.8x-%4.4x-%4.4x-%2.2x%2.2x-",
	  u.time_low,
	  u.time_mid,
	  u.time_hi_and_version,
	  u.clock_seq_hi_and_reserved,
	  u.clock_seq_low);
  tmp += 24;
  for (i = 0; i < 6; i++) {
    sprintf(tmp,"%2.2x", u.node[i]);
    tmp += 2;
  }
  *tmp = 0;
  
}

/* get-current_time -- get time as 60-bit 100ns ticks since UUID epoch.
   Compensate for the fact that real clock resolution is
   less than 100ns. */
static void get_current_time(uuid_time_t *timestamp)
{
  static int inited = 0;
  static uuid_time_t time_last;
  static uint16_t uuids_this_tick;
  uuid_time_t time_now;

  if (!inited) {
    get_system_time(&time_now);
    uuids_this_tick = UUIDS_PER_TICK;
    inited = 1;
  }

  for ( ; ; ) {
    get_system_time(&time_now);

    /* if clock reading changed since last UUID generated, */
    if (time_last != time_now) {
      /* reset count of uuids gen'd with this clock reading */
      uuids_this_tick = 0;
      time_last = time_now;
      break;
    }
    if (uuids_this_tick < UUIDS_PER_TICK) {
      uuids_this_tick++;
      break;
    }
    /* going too fast for our clock; spin */
  }
  /* add the count of uuids to low order bits of the clock reading */
  *timestamp = time_now + uuids_this_tick;
}


/* system dependent call to get IEEE node ID.
   This sample implementation generates a random node ID. */
/* netperf mod - don't bother trying to read or write the nodeid */
static void get_ieee_node_identifier(uuid_node_t *node)
{
  static int inited = 0;
  static uuid_node_t saved_node;
  char seed[16];

  if (!inited) {
    get_random_info(seed);
    seed[0] |= 0x01;
    memcpy(&saved_node, seed, sizeof saved_node);
  }
  inited = 1;

  *node = saved_node;
}


/* format_uuid_v1 -- make a UUID from the timestamp, clockseq,
   and node ID */
static void format_uuid_v1(uuid_t* uuid, uint16_t clock_seq,
                    uuid_time_t timestamp, uuid_node_t node)
{
  /* Construct a version 1 uuid with the information we've gathered
     plus a few constants. */
  uuid->time_low = (unsigned long)(timestamp & 0xFFFFFFFF);
  uuid->time_mid = (unsigned short)((timestamp >> 32) & 0xFFFF);
  uuid->time_hi_and_version =
    (unsigned short)((timestamp >> 48) & 0x0FFF);
  uuid->time_hi_and_version |= (1 << 12);
  uuid->clock_seq_low = clock_seq & 0xFF;
  uuid->clock_seq_hi_and_reserved = (clock_seq & 0x3F00) >> 8;
  uuid->clock_seq_hi_and_reserved |= 0x80;
  memcpy(&uuid->node, &node, sizeof uuid->node);
}

/* uuid_create -- generator a UUID */
int uuid_create(uuid_t *uuid)
{
  uuid_time_t timestamp;
  uint16_t clockseq;
  uuid_node_t node;
  
  /* get time, node ID, saved state from non-volatile storage */
  get_current_time(&timestamp);
  get_ieee_node_identifier(&node);
  
  /* for us clockseq is always to be random as we have no state */
  clockseq = true_random();
  
  /* stuff fields into the UUID */
  format_uuid_v1(uuid, clockseq, timestamp, node);
  return 1;
}

void get_uuid_string(char *uuid_str, size_t size) {
  uuid_t u;

  uuid_create(&u);
  snpuid(uuid_str,size,u);
  
  return;
}

#ifdef NETPERF_STANDALONE_DEBUG

int
main(int argc, char *argv[])
{
  uuid_t u;
  char  uuid_str[38];
#if 0
  uuid_create(&u);
  printf("uuid_create(): "); puid(u);
  snpuid(uuid_str,sizeof(uuid_str),u);
  printf("\nas a string %s\n",uuid_str);
#endif
  get_uuid_string(uuid_str,sizeof(uuid_str));
  printf("uuid_str is %s\n",uuid_str);
  return 0;
}


#endif
