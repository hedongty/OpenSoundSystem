/*
 * Purpose: Client/server audio device pair for oss_userdev
 *
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2008. All rights reserved.

#include "oss_userdev_cfg.h"
#include <oss_userdev_exports.h>
#include "userdev.h"
static void userdev_free_device_pair (userdev_devc_t *devc);

extern int userdev_visible_clientnodes;

static void
transfer_audio (userdev_portc_t * server_portc, dmap_t * dmap_from,
		dmap_t * dmap_to)
{
  int l = dmap_from->fragment_size;
  unsigned char *fromp, *top;

  if (dmap_to->fragment_size != l)
    {
      cmn_err (CE_WARN, "Fragment size mismatch (%d != %d)\n",
	       dmap_to->fragment_size, l);

      /* Perform emergency stop */
      server_portc->input_triggered = 0;
      server_portc->output_triggered = 0;
      server_portc->peer->input_triggered = 0;
      server_portc->peer->output_triggered = 0;
      return;
    }

  fromp =
    dmap_from->dmabuf + (dmap_from->byte_counter % dmap_from->bytes_in_use);
  top = dmap_to->dmabuf + (dmap_to->byte_counter % dmap_to->bytes_in_use);

  memcpy (top, fromp, l);

}

static void
handle_input (userdev_portc_t * server_portc)
{
  userdev_portc_t *client_portc = server_portc->peer;

  if (client_portc->output_triggered)
    {
      transfer_audio (server_portc,
		      audio_engines[client_portc->audio_dev]->dmap_out,
		      audio_engines[server_portc->audio_dev]->dmap_in);
      oss_audio_outputintr (client_portc->audio_dev, 0);
    }

  oss_audio_inputintr (server_portc->audio_dev, 0);
}

static void
handle_output (userdev_portc_t * server_portc)
{
  userdev_portc_t *client_portc = server_portc->peer;

  if (client_portc->input_triggered)
    {
      transfer_audio (server_portc,
		      audio_engines[server_portc->audio_dev]->dmap_out,
		      audio_engines[client_portc->audio_dev]->dmap_in);
      oss_audio_inputintr (client_portc->audio_dev, 0);
    }

  oss_audio_outputintr (server_portc->audio_dev, 0);
}

static void
userdev_cb (void *pc)
{
/*
 * This timer callback routine will get called 100 times/second. It handles
 * movement of audio data between the client and server sides.
 */
  userdev_portc_t *server_portc = pc;
  userdev_devc_t *devc = server_portc->devc;
  int tmout = devc->poll_ticks;

  if (tmout < 1)
    tmout = 1;

  devc->timeout_id = 0;	/* No longer valid */

  if (server_portc->input_triggered)
    handle_input (server_portc);

  if (server_portc->output_triggered)
    handle_output (server_portc);

  /* Retrigger timer callback */
  if (server_portc->input_triggered || server_portc->output_triggered)
    devc->timeout_id = timeout (userdev_cb, server_portc, tmout);
}

static int
userdev_check_input (int dev)
{
  userdev_portc_t *portc = audio_engines[dev]->portc;
  if (!portc->peer->output_triggered)
    {
      return OSS_ECONNRESET;
    }
  return 0;
}

static int
userdev_check_output (int dev)
{
  userdev_portc_t *portc = audio_engines[dev]->portc;

  if (!portc->peer->input_triggered)
    {
      return OSS_ECONNRESET;
    }

  if (portc->peer->open_mode == 0)
    return OSS_EIO;
  return 0;
}

static void
setup_sample_format (userdev_portc_t * portc)
{
  adev_t *adev;
  userdev_devc_t *devc = portc->devc;
  int fragsize, frame_size;

  frame_size = devc->channels * devc->fmt_bytes;
  if (frame_size == 0)
    frame_size = 4;

  fragsize = (devc->rate * frame_size * devc->poll_ticks) / OSS_HZ;	/* Number of bytes/fragment */
  devc->rate = fragsize * 100 / frame_size;

cmn_err(CE_CONT, "Rate = %d, frag=%d\n", devc->rate, fragsize);
/* Setup the server side */
  adev = audio_engines[portc->audio_dev];
  adev->min_block = adev->max_block = fragsize;

/* Setup the client side */
  adev = audio_engines[portc->peer->audio_dev];
  adev->min_block = adev->max_block = fragsize;

  adev->max_rate = adev->min_rate = devc->rate;
  adev->iformat_mask = devc->fmt;
  adev->oformat_mask = devc->fmt;
  adev->xformat_mask = devc->fmt;
  adev->min_channels = adev->max_channels = devc->channels;
}

static int
userdev_server_set_rate (int dev, int arg)
{
  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;

  if (arg == 0)
    return devc->rate;

  if (portc->peer->input_triggered || portc->peer->output_triggered)
    return devc->rate;

  if (arg < 5000)
    arg = 5000;
  if (arg > MAX_RATE)
    arg = MAX_RATE;

  /* Force the sample rate to be multiple of 100 */
  arg = (arg / 100) * 100;

  devc->rate = arg;

  setup_sample_format (portc);

  return devc->rate = arg;
}

/*ARGSUSED*/
static int
userdev_client_set_rate (int dev, int arg)
{
  userdev_devc_t *devc = audio_engines[dev]->devc;

  return devc->rate;
}

static short
userdev_server_set_channels (int dev, short arg)
{
  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;

  if (arg == 0)
    return devc->channels;

  if (portc->peer->input_triggered || portc->peer->output_triggered)
    return devc->channels;

  if (arg < 1)
    arg = 1;
  if (arg > MAX_CHANNELS)
    arg = MAX_CHANNELS;

  devc->channels = arg;

  setup_sample_format (portc);

  return devc->channels;
}

/*ARGSUSED*/
static short
userdev_client_set_channels (int dev, short arg)
{
  userdev_devc_t *devc = audio_engines[dev]->devc;

  return devc->channels;	/* Server side channels */
}

static unsigned int
userdev_server_set_format (int dev, unsigned int arg)
{
  userdev_devc_t *devc = audio_engines[dev]->devc;
  userdev_portc_t *portc = audio_engines[dev]->portc;

  if (arg == 0)
    return devc->fmt;

  if (portc->peer->input_triggered || portc->peer->output_triggered)
    return devc->fmt;

  switch (arg)
    {
    case AFMT_S16_NE:
      devc->fmt_bytes = 2;
      break;

    case AFMT_S32_NE:
      devc->fmt_bytes = 4;
      break;

    default:			/* Unsupported format */
      arg = AFMT_S16_NE;
      devc->fmt_bytes = 2;

    }

  devc->fmt = arg;

  setup_sample_format (portc);

  return devc->fmt;
}

/*ARGSUSED*/
static unsigned int
userdev_client_set_format (int dev, unsigned int arg)
{
  userdev_devc_t *devc = audio_engines[dev]->devc;

  return devc->fmt;	/* Server side sample format */
}

static void userdev_trigger (int dev, int state);

static void
userdev_reset (int dev)
{
  userdev_trigger (dev, 0);
}

/*ARGSUSED*/
static int
userdev_server_open (int dev, int mode, int open_flags)
{
  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;
  oss_native_word flags;
cmn_err(CE_CONT, "Server open %d\n", dev);

  if (portc == NULL || portc->peer == NULL)
    return OSS_ENXIO;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);

  if (portc->open_mode)
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return OSS_EBUSY;
    }

  portc->open_mode = mode;

  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  devc->open_count++;

  return 0;
}

/*ARGSUSED*/
static int
userdev_client_open (int dev, int mode, int open_flags)
{
  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;
  oss_native_word flags;
cmn_err(CE_CONT, "Client open %d\n", dev);

  if (portc == NULL || portc->peer == NULL)
    return OSS_ENXIO;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);

  if (portc->open_mode)
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return OSS_EBUSY;
    }

  portc->open_mode = mode;
  devc->open_count++;

  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
  return 0;
}

/*ARGSUSED*/
static void
userdev_server_close (int dev, int mode)
{
  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;
  oss_native_word flags;
  int open_count;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  portc->open_mode = 0;

  /* Stop the client side because there is no server */
  portc->peer->input_triggered = 0;
  portc->peer->output_triggered = 0;
  open_count = --devc->open_count;

  if (open_count == 0)
     userdev_free_device_pair (devc);
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}

/*ARGSUSED*/
static void
userdev_client_close (int dev, int mode)
{
  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;
  oss_native_word flags;
  int open_count;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  portc->open_mode = 0;

  open_count = --devc->open_count;

  if (open_count == 0)
     userdev_free_device_pair (devc);

  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}

/*ARGSUSED*/
static int
userdev_ioctl (int dev, unsigned int cmd, ioctl_arg arg)
{
  switch (cmd)
    {
    case SNDCTL_GETLABEL:
      {
	/*
	 * Return an empty string so that this feature can be tested.
	 * Complete functionality is to be implemented later.
	 */
	oss_label_t *s = (oss_label_t *) arg;
	memset (s, 0, sizeof (oss_label_t));
	return 0;
      }
      break;

    case SNDCTL_GETSONG:
      {
	/*
	 * Return an empty string so that this feature can be tested.
	 * Complete functionality is to be implemented later.
	 */
	oss_longname_t *s = (oss_longname_t *) arg;
	memset (s, 0, sizeof (oss_longname_t));
	return 0;
      }
      break;
    }

  return OSS_EINVAL;
}

static void
set_adev_name(int dev, const char *name)
{
  adev_t *adev = audio_engines[dev];

  strcpy(adev->name, name);

#ifdef CONFIG_OSS_VMIX
  if (adev->vmix_mixer != NULL)
     vmix_change_devnames(adev->vmix_mixer, name);
#endif
	
}

static int
create_instance(int dev, userdev_create_t *crea)
{
  userdev_devc_t *devc = audio_engines[dev]->devc;
  char tmp_name[64];

  devc->match_method = crea->match_method;
  devc->match_key = crea->match_key;
  devc->create_flags = crea->flags;

  devc->poll_ticks = (crea->poll_interval * OSS_HZ) / 1000;
cmn_err(CE_CONT, "Set poll_ticks=%u, interval=%u, HZ=%d\n", devc->poll_ticks, crea->poll_interval, OSS_HZ);

  if (devc->poll_ticks < 1) 
     devc->poll_ticks = 1;

  crea->poll_interval = (1000*devc->poll_ticks) / OSS_HZ;

  if (crea->poll_interval<1)
     crea->poll_interval = 1;
cmn_err(CE_CONT, "Return poll interval %u\n", crea->poll_interval);

  crea->name[sizeof(crea->name)-1]=0; /* Overflow protectgion */

  sprintf(tmp_name, "%s (server)", crea->name);
  tmp_name[49]=0;
  set_adev_name (devc->client_portc.audio_dev, crea->name);
  set_adev_name (devc->server_portc.audio_dev, tmp_name);
cmn_err(CE_CONT, "Match method %d, key %d\n", devc->match_method, devc->match_key);

  strcpy(crea->devnode, audio_engines[devc->client_portc.audio_dev]->devnode);

  return 0;
}

/*ARGSUSED*/
static int
userdev_server_ioctl (int dev, unsigned int cmd, ioctl_arg arg)
{
  switch (cmd)
    {
    case USERDEV_CREATE_INSTANCE:
	    {
		userdev_create_t *crea = (userdev_create_t *)arg;

	    	return create_instance(dev, crea);
	    }
	    break;

    case USERDEV_GET_CLIENTCOUNT:
	    {
  		userdev_devc_t *devc = audio_engines[dev]->devc;

		return *arg = (devc->client_portc.open_mode != 0);
	    }
	    break;
    }

  return userdev_ioctl(dev, cmd, arg);
}

/*ARGSUSED*/
static void
userdev_output_block (int dev, oss_native_word buf, int count, int fragsize,
			int intrflag)
{
}

/*ARGSUSED*/
static void
userdev_start_input (int dev, oss_native_word buf, int count, int fragsize,
		       int intrflag)
{
}

static void
userdev_trigger (int dev, int state)
{
  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;

  if (portc->open_mode & OPEN_READ)	/* Handle input */
    {
      portc->input_triggered = !!(state & OPEN_READ);
    }

  if (portc->open_mode & OPEN_WRITE)	/* Handle output */
    {
      portc->output_triggered = !!(state & OPEN_WRITE);
    }

  if (portc->output_triggered || portc->input_triggered)	/* Something is going on */
    {
      int tmout = devc->poll_ticks;

      if (tmout < 1)
	tmout = 1;

      if (portc->port_type != PT_SERVER)
	portc = portc->peer;	/* Switch to the server side */

      if (portc->output_triggered || portc->input_triggered)	/* Something is going on */
	if (devc->timeout_id == 0)
	{
	  devc->timeout_id = timeout (userdev_cb, portc, tmout);
	}
    }
  else
    {
      if (portc->port_type == PT_SERVER)
	if (devc->timeout_id != 0)
	  {
	    untimeout (devc->timeout_id);
	    devc->timeout_id = 0;
	  }
    }
}

/*ARGSUSED*/
static int
userdev_server_prepare_for_input (int dev, int bsize, int bcount)
{
  oss_native_word flags;

  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  portc->input_triggered = 0;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  return 0;
}

/*ARGSUSED*/
static int
userdev_server_prepare_for_output (int dev, int bsize, int bcount)
{
  oss_native_word flags;

  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  portc->output_triggered = 0;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  return 0;
}

/*ARGSUSED*/
static int
userdev_client_prepare_for_input (int dev, int bsize, int bcount)
{
  oss_native_word flags;
  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  portc->input_triggered = 0;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  return 0;
}

/*ARGSUSED*/
static int
userdev_client_prepare_for_output (int dev, int bsize, int bcount)
{
  oss_native_word flags;
  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  portc->output_triggered = 0;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  return 0;
}

/*ARGSUSED*/
static int
userdev_alloc_buffer (int dev, dmap_t * dmap, int direction)
{
#define MY_BUFFSIZE (64*1024)
  if (dmap->dmabuf != NULL)
    return 0;
  dmap->dmabuf_phys = 0;	/* Not mmap() capable */
  dmap->dmabuf = KERNEL_MALLOC (MY_BUFFSIZE);
  if (dmap->dmabuf == NULL)
    return OSS_ENOSPC;
  dmap->buffsize = MY_BUFFSIZE;

  return 0;
}

/*ARGSUSED*/
static int
userdev_free_buffer (int dev, dmap_t * dmap, int direction)
{
  if (dmap->dmabuf == NULL)
    return 0;
  KERNEL_FREE (dmap->dmabuf);

  dmap->dmabuf = NULL;
  return 0;
}

#if 0
static int
userdev_get_buffer_pointer (int dev, dmap_t * dmap, int direction)
{
}
#endif

/*ARGSUSED*/
static int
userdev_ioctl_override (int dev, unsigned int cmd, ioctl_arg arg)
{
//cmn_err(CE_CONT, "userdev_ioctl_override(%d, %08x)\n", dev, cmd);
  switch (cmd)
  {
  case SNDCTL_MIX_NRMIX:
	  return *arg=1;
	  break;

  default:
	  return OSS_EAGAIN;
  }
}

static audiodrv_t userdev_server_driver = {
  userdev_server_open,
  userdev_server_close,
  userdev_output_block,
  userdev_start_input,
  userdev_server_ioctl,
  userdev_server_prepare_for_input,
  userdev_server_prepare_for_output,
  userdev_reset,
  NULL,
  NULL,
  NULL,
  NULL,
  userdev_trigger,
  userdev_server_set_rate,
  userdev_server_set_format,
  userdev_server_set_channels,
  NULL,
  NULL,
  userdev_check_input,
  userdev_check_output,
  userdev_alloc_buffer,
  userdev_free_buffer,
  NULL,
  NULL,
  NULL				/* userdev_get_buffer_pointer */
};

static audiodrv_t userdev_client_driver = {
  userdev_client_open,
  userdev_client_close,
  userdev_output_block,
  userdev_start_input,
  userdev_ioctl,
  userdev_client_prepare_for_input,
  userdev_client_prepare_for_output,
  userdev_reset,
  NULL,
  NULL,
  NULL,
  NULL,
  userdev_trigger,
  userdev_client_set_rate,
  userdev_client_set_format,
  userdev_client_set_channels,
  NULL,
  NULL,
  userdev_check_input,
  userdev_check_output,
  userdev_alloc_buffer,
  userdev_free_buffer,
  NULL,
  NULL,
  NULL,	// userdev_get_buffer_pointer
  NULL,	// userdev_calibrate_speed
  NULL,	// userdev_sync_control
  NULL,	// userdev_prepare_to_stop
  NULL,	// userdev_get_input_pointer
  NULL,	// userdev_get_output_pointer
  NULL,	// userdev_bind
  NULL,	// userdev_setup_fragments
  NULL,	// userdev_redirect
  userdev_ioctl_override
};


static int
install_server (userdev_devc_t * devc)
{
  userdev_portc_t *portc = &devc->server_portc;
  int adev;

  int opts =
    ADEV_STEREOONLY | ADEV_16BITONLY | ADEV_VIRTUAL |
    ADEV_FIXEDRATE | ADEV_SPECIAL | ADEV_HIDDEN | ADEV_DUPLEX;

  memset (portc, 0, sizeof (*portc));

  portc->devc = devc;
  portc->port_type = PT_SERVER;

  if ((adev = oss_install_audiodev (OSS_AUDIO_DRIVER_VERSION,
				    devc->osdev,
				    devc->osdev,
				    "User space audio device server side",
				    &userdev_server_driver,
				    sizeof (audiodrv_t),
				    opts, SUPPORTED_FORMATS, devc, -1)) < 0)
    {
      return adev;
    }

  audio_engines[adev]->portc = portc;
  audio_engines[adev]->min_rate = 5000;
  audio_engines[adev]->max_rate = MAX_RATE;
  audio_engines[adev]->min_channels = 1;
  audio_engines[adev]->max_channels = MAX_CHANNELS;
  strcpy(audio_engines[adev]->devnode, userdev_server_devnode);

  portc->audio_dev = adev;

  return adev;
}


static int
install_client (userdev_devc_t * devc)
{
  userdev_portc_t *portc = &devc->client_portc;
  int adev;

  int opts =
    ADEV_STEREOONLY | ADEV_16BITONLY | ADEV_VIRTUAL | ADEV_DUPLEX |
    ADEV_FIXEDRATE | ADEV_SPECIAL | ADEV_LOOP;

  memset (portc, 0, sizeof (*portc));

  portc->devc = devc;
  portc->port_type = PT_CLIENT;

  if (!userdev_visible_clientnodes && !(devc->create_flags & USERDEV_F_VMIX_PRIVATENODE))
  {
     opts |= ADEV_HIDDEN;
cmn_err(CE_CONT, "Create hidden device (%x, %x)\n", userdev_visible_clientnodes,devc->create_flags);
  }
else cmn_err(CE_CONT, "Create visible device\n");

  if ((adev = oss_install_audiodev (OSS_AUDIO_DRIVER_VERSION,
				    devc->osdev,
				    devc->osdev,
				    "User space audio device",
				    &userdev_client_driver,
				    sizeof (audiodrv_t),
				    opts, SUPPORTED_FORMATS, devc, -1)) < 0)
    {
      return adev;
    }

  if (!userdev_visible_clientnodes) /* Invisible client device nodes */
     strcpy(audio_engines[adev]->devnode, userdev_client_devnode);

  audio_engines[adev]->portc = portc;
  audio_engines[adev]->min_rate = 5000;
  audio_engines[adev]->max_rate = MAX_RATE;
  audio_engines[adev]->min_channels = 1;
  audio_engines[adev]->max_channels = MAX_CHANNELS;

  portc->audio_dev = adev;
#ifdef CONFIG_OSS_VMIX
  vmix_attach_audiodev(devc->osdev, adev, -1, 0);
#endif

  return adev;
}

int
userdev_create_device_pair(void)
{
  int client_engine, server_engine;
  userdev_devc_t *devc;
  oss_native_word flags;

  if ((devc=PMALLOC(userdev_osdev, sizeof (*devc))) == NULL)
     return OSS_ENOMEM;
  memset(devc, 0, sizeof(*devc));

  devc->osdev = userdev_osdev;
  MUTEX_INIT (devc->osdev, devc->mutex, MH_DRV);
  devc->active=1;

  devc->rate = 48000;
  devc->fmt = AFMT_S16_NE;
  devc->fmt_bytes = 2;
  devc->channels = 2;
  devc->poll_ticks = 10;

  if ((server_engine=install_server (devc)) < 0)
     return server_engine;

  if ((client_engine=install_client (devc)) < 0)
	return client_engine;

  devc->client_portc.peer = &devc->server_portc;
  devc->server_portc.peer = &devc->client_portc;

  /*
   * Insert the device to the list of available devices
   */
  MUTEX_ENTER_IRQDISABLE(userdev_global_mutex, flags);
  devc->next_instance = userdev_active_device_list;
  userdev_active_device_list = devc;
  MUTEX_EXIT_IRQRESTORE(userdev_global_mutex, flags);
cmn_err(CE_CONT, "Created new device pair, server=%d, client=%d\n", server_engine, client_engine);

  return server_engine;
}

static void
userdev_free_device_pair (userdev_devc_t *devc)
{
  oss_native_word flags;

  set_adev_name(devc->client_portc.audio_dev, "User space audio device");
  set_adev_name(devc->server_portc.audio_dev, "User space audio device server side");

cmn_err(CE_CONT, "userdev_free_device_pair(%p)\n", devc);
  MUTEX_ENTER_IRQDISABLE(userdev_global_mutex, flags);

  devc->match_method = 0;
  devc->match_key = 0;

  /*
   * Add to the free device pair list.
   */
  devc->next_instance = userdev_free_device_list;
  userdev_free_device_list = devc;

  /*
   * Remove the device pair from the active device list.
   */

  if (userdev_active_device_list == devc) /* First device in the list */
     {
	     userdev_active_device_list = userdev_active_device_list->next_instance;
cmn_err(CE_CONT,"Removed %p from active devices (first)\n", devc);
     }
  else
     {
	     userdev_devc_t *this = userdev_active_device_list, *prev = NULL;

	     while (this != NULL)
	     {
		     if (this == devc)
			{
				prev->next_instance = this->next_instance; /* Remove */
cmn_err(CE_CONT, "Removed %p from active devices\n", devc);
				break;
			}

		     prev = this;
		     this = this->next_instance;
	     }
     }
  MUTEX_EXIT_IRQRESTORE(userdev_global_mutex, flags);
}

void
userdev_delete_device_pair(userdev_devc_t *devc)
{
  if (!devc->active)
     return;

  devc->active = 0;
  MUTEX_CLEANUP(devc->mutex);
}

int
usrdev_find_free_device_pair(void)
{
  oss_native_word flags;
  userdev_devc_t *devc;

cmn_err(CE_CONT, "usrdev_find_free_device_pair()\n");

  MUTEX_ENTER_IRQDISABLE(userdev_global_mutex, flags);

  if (userdev_free_device_list != NULL)
     {
	devc = userdev_free_device_list;
	userdev_free_device_list = userdev_free_device_list->next_instance;

  	devc->next_instance = userdev_active_device_list;
  	userdev_active_device_list = devc;

  	MUTEX_EXIT_IRQRESTORE(userdev_global_mutex, flags);
cmn_err(CE_CONT, "Reuse %d\n", devc->server_portc.audio_dev);
	return devc->server_portc.audio_dev;
     }
  MUTEX_EXIT_IRQRESTORE(userdev_global_mutex, flags);

  return OSS_ENXIO;
}
