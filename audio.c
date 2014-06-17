#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
#include <glib.h>
#include <linux/videodev.h>
#include <sys/ioctl.h>
#include <glib/gprintf.h>

#include "fmtx-object.h"
#include "audio.h"

static void pa_connect(FmtxObject *obj);

#define WRITE_FMTX_SYSFS_PILOT(file, val) \
  WRITE_FMTX_SYSFS(file, val, "fmtxd fmtx chirping error")

gboolean
idle_timeout_cb(FmtxObject *obj)
{
  obj->active = 0;
  obj->idle_timeout = 0;
  obj->exit_timeout = g_timeout_add(60000, (GSourceFunc)exit_timeout_cb, obj);
  return FALSE;
}

static gboolean
pilot_timeout_cb(FmtxObject *obj)
{

  if (!obj->active &&
      (!obj->mixer_inited || !obj->pa_running) &&
      g_str_equal(obj->state, "enabled"))
  {
    fmtx_enable(obj, FALSE);
    g_idle_add(emit_changed, obj);
    g_idle_add(emit_info, obj);
    obj->active = TRUE;

    if(!obj->idle_timeout)
      obj->idle_timeout = g_timeout_add_seconds(300,
                                                (GSourceFunc)idle_timeout_cb,
                                                obj);
  }

  obj->pilot_timeout = 0;

  return FALSE;
}

static void
fmtx_set_mute(FmtxObject *obj, int value)
{
  struct v4l2_control ctl;

  /* FIXME */
  ctl.id = 0x980909u;
  ctl.value = value;

  if(ioctl(obj->dev_radio, VIDIOC_S_CTRL, &ctl) < 0)
    g_fprintf(stderr, "Could not toggle mute on the device\n");
}

int
fmtx_set_frequency(FmtxObject *fmtx, unsigned int frequency)
{
  unsigned int f;
  struct video_tuner tun;
  GError *err = NULL;

  if ( fmtx->dev_radio < 0 )
    return 1;

  if ( fmtx->freq_max < fmtx->freq_min )
    return 0;

  f = fmtx->freq_min;

  while ( f <= fmtx->freq_max )
  {
    if ( frequency == f)
      break;

    f += fmtx->freq_step;
  }

  if (f > fmtx->freq_max)
    return 0;

  fmtx->frequency = f;

  gconf_client_set_int(fmtx->gcclient, "/system/fmtx/frequency", f, &err);

  if(err)
    g_fprintf(stderr, "Could not set fmtx settings: %s\n", err->message);

  if(!g_str_equal(fmtx->state, "enabled"))
    return 2;

  tun.tuner = 0;

  if(ioctl(fmtx->dev_radio, VIDIOCSTUNER, &tun) >= 0 &&
     ioctl(fmtx->dev_radio, VIDIOCGTUNER, &tun) >= 0)
  {
    f = rint((tun.flags & VIDEO_TUNER_LOW ? 16000.0 : 16.0) *
             (long double)fmtx->frequency / 1000.0);

    if(ioctl(fmtx->dev_radio, VIDIOCSFREQ, &f) >= 0)
      return 2;
  }

  perror("fmtxd Could not set frequency");
  return 1;
}

int
fmtx_enable(FmtxObject *fmtx, gboolean enable)
{
  int rv;
  GError *err = NULL;

  if(!g_str_equal(fmtx->state, "n/a"))
  {
    if(enable)
    {
      if(!g_str_equal(fmtx->state, "enabled"))
      {
        if ( fmtx->offline || fmtx->hp_connected )
        {
          fmtx_set_mute(fmtx, TRUE);
          g_free(fmtx->state);
          fmtx->state = g_strdup("disabled");
          return 0;
        }
        fmtx_set_mute(fmtx, FALSE);
        rv = fmtx_set_frequency(fmtx, fmtx->frequency);
        if(rv != 2)
        {
          fmtx_set_mute(fmtx, TRUE);
          return rv;
        }
        g_free(fmtx->state);
        fmtx->state = g_strdup("enabled");
out:
        gconf_client_set_bool(fmtx->gcclient, "/system/fmtx/enabled", enable,
                              &err);
        if(err)
          g_fprintf(stderr, "Could not save fmtx settings: %s\n", err->message);
        fmtx_toggle_pilot(fmtx);
        fmtx_set_frequency(fmtx, fmtx->frequency);
        return 2;
      }
    }
    else
    {
      if(!g_str_equal(fmtx->state, "disabled"))
      {
        fmtx->active = FALSE;
        if(fmtx->idle_timeout)
        {
          g_source_remove(fmtx->idle_timeout);
          fmtx->idle_timeout = 0;
        }
        fmtx_set_mute(fmtx, TRUE);
        g_free(fmtx->state);
        fmtx->state = g_strdup("disabled");
        goto out;
      }
      fmtx->active = 0;
      if(fmtx->idle_timeout)
      {
        g_source_remove(fmtx->idle_timeout);
        fmtx->idle_timeout = 0;
        return 2;
      }
    }
    return 2;
  }
  return 0;
}

void
fmtx_toggle_pilot(FmtxObject *fmtx)
{
  if(!g_str_equal(fmtx->state, "enabled") ||
     (fmtx->mixer_inited && fmtx->pa_running))
  {
    if(fmtx->active &&
       fmtx->pa_running &&
       !fmtx->offline &&
       !fmtx->hp_connected &&
       !fmtx->call_active)
    {
      if(fmtx->idle_timeout)
      {
        g_source_remove(fmtx->idle_timeout);
        fmtx->idle_timeout = 0;
      }

      fmtx->active = 0;
      fmtx_enable(fmtx, TRUE);
      g_idle_add(emit_changed, fmtx);
      g_idle_add(emit_info, fmtx);
    }
    WRITE_FMTX_SYSFS_PILOT("tone_frequency", "0");
    WRITE_FMTX_SYSFS_PILOT("tone_deviation", "0");
    WRITE_FMTX_SYSFS_PILOT("tone_off_time", "0");
    WRITE_FMTX_SYSFS_PILOT("tone_on_time", "0");
  }
  else
  {
    WRITE_FMTX_SYSFS_PILOT("tone_frequency", "1760");
    WRITE_FMTX_SYSFS_PILOT("tone_deviation", "6750");
    WRITE_FMTX_SYSFS_PILOT("tone_off_time", "2000");
    WRITE_FMTX_SYSFS_PILOT("tone_on_time", "50");

    if(!fmtx->pilot_timeout)
      fmtx->pilot_timeout = g_timeout_add(50000,
                                          (GSourceFunc)pilot_timeout_cb,
                                          fmtx);
  }
}


static void
context_sink_info_cb(pa_context *c, const pa_sink_info *i, int eol,
                     void *userdata)
{
  if(!eol)
  {
    ((FmtxObject*)userdata)->pa_running = (i->state == PA_SINK_RUNNING);
    fmtx_toggle_pilot(userdata);
  }
}

static void
context_subscribe_cb(pa_context *c, pa_subscription_event_type_t t,
                     uint32_t idx, void *userdata)
{
  pa_operation *op;

  op = pa_context_get_sink_info_by_name(c, "sink.hw0", context_sink_info_cb,
                                        userdata);
  pa_operation_unref(op);
}

static void
context_state_cb(pa_context *c, void *userdata)
{
  pa_context_state_t state;
  pa_operation *op;

  state = pa_context_get_state(c);

  if(state >= PA_CONTEXT_READY)
  {
    if( state == PA_CONTEXT_READY)
    {
      pa_context_set_subscribe_callback(c, context_subscribe_cb, userdata);

      op = pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK, 0, userdata);
      pa_operation_unref(op);

      op = pa_context_get_sink_info_by_name(c, "sink.hw0", context_sink_info_cb,
                                            userdata);
      pa_operation_unref(op);
    }
    else
      pa_connect((FmtxObject *)userdata);
  }
}

static void
pa_connect(FmtxObject *obj)
{
  if(obj->context)
    pa_context_unref(obj->context);

  obj->context = pa_context_new(obj->api, "fmtx-middleware");

  pa_context_set_state_callback(obj->context, context_state_cb, obj);
  if(pa_context_connect(obj->context, 0,
                        PA_CONTEXT_NOFAIL|PA_CONTEXT_NOAUTOSPAWN, 0) < 0)
    g_log(NULL, G_LOG_LEVEL_WARNING,
          "Failed to connect pa server: %s",
          pa_strerror(pa_context_errno(obj->context)));
}

void register_pa(FmtxObject *obj)
{
  pa_glib_mainloop *m;

  m = pa_glib_mainloop_new(g_main_context_default());

  g_assert(m);

  obj->api = pa_glib_mainloop_get_api(m);

  g_assert(obj->api);

  pa_connect(obj);
}
