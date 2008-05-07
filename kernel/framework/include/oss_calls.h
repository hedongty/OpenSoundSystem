/*
 * Purpose: Prototypes for various internal routines of OSS. 
 *
 */
#define COPYING19 Copyright (C) Hannu Savolainen and Dev Mazumdar 1996-2007. All rights reserved.

/*
 * sndstat.c
 */
void store_msg (char *msg);
#ifdef DO_TIMINGS
/* Run time debugging stuff (for testing purposes only) */
void oss_do_timing (char *txt);
void oss_do_timing2 (int timing_mask, char *txt);
void timing_set_device (int dev, dmap_t * dmap);
typedef oss_native_word (*oss_timing_timer_func) (void *);
void timing_install_timer (oss_timing_timer_func, void *);
void oss_timing_enter (int bin);
void oss_timing_leave (int bin);
void timing_open (void);
void timing_close (void);
#endif

#ifdef LICENSED_VERSION
typedef int (*put_status_func_t) (const char *s);
typedef int (*put_status_int_t) (unsigned int val, int radix);
extern void oss_print_license (put_status_func_t put_status,
			       put_status_int_t put_status_int);
extern int oss_license_handle_time (time_t t);
#endif

extern void install_sndstat (oss_device_t * osdev);
extern void install_dev_mixer (oss_device_t * osdev);

/*
 * vmix_core.c
 */

extern void vmix_uninit (void);

/*
 * oss_audio_core.c
 */

extern void oss_audio_uninit (void);
