#include <sys/ioctl.h>
#include <mce/dbus-names.h>
#include <mce/mode-names.h>

#include "fmtx-object.h"
#include "dbus.h"
#include "audio.h"

static void sig_device_mode_ind_cb(DBusGProxy *proxy, const char *valueName,
                                   FmtxObject *obj)
{
  if ( g_str_equal(MCE_NORMAL_MODE, valueName) )
  {
    obj->offline = 0;
  }
  else
  {
    obj->offline = 1;
    if ( g_str_equal(obj->state, "enabled") )
    {
      fmtx_enable(obj, FALSE);
      g_idle_add(emit_changed, obj);
      g_idle_add((GSourceFunc)emit_info, obj);
    }
  }
}

static void
usb_device_1d6b_2_musb_hdrc_cb(DBusGProxy *proxy, const gchar *condition,
                               const gchar *details, FmtxObject *obj)
{
  DBusGProxy *hal_proxy;
  gchar *usb_state;
  GError *err = NULL;

  hal_proxy = dbus_g_proxy_new_for_name(
         obj->dbus,
         "org.freedesktop.Hal",
         "/org/freedesktop/Hal/devices/usb_device_1d6b_2_musb_hdrc",
         "org.freedesktop.Hal.Device");
  if(hal_proxy)
  {
    dbus_g_proxy_call(hal_proxy,
                      "GetPropertyString", &err,
                      G_TYPE_STRING, "usb_device.mode", G_TYPE_INVALID,
                      G_TYPE_STRING, &usb_state, G_TYPE_INVALID);
    if ( err )
    {
      log_error("Unable to get device state", "", 0);
      g_clear_error(&err);
    }
    else
    {
      g_object_unref(hal_proxy);

      if(g_str_equal("b_idle", usb_state))
      {
        obj->usb_connected = 0;
        if(obj->active && !obj->hp_connected && !obj->call_active)
        {
          if(obj->idle_timeout)
          {
            g_source_remove(obj->idle_timeout);
            obj->idle_timeout = 0;
          }

          obj->active = 0;
          fmtx_enable(obj, 1);
          g_idle_add(emit_changed, obj);
          g_idle_add((GSourceFunc)emit_info, obj);
        }
      }
      else
      {
        obj->usb_connected = 1;

        if(g_str_equal(obj->state, "enabled"))
        {
          g_signal_emit(obj, FMTX_OBJECT_GET_CLASS(obj)->error, 0,
                        "fmtx_ni_usb_error");
          fmtx_enable(obj, 0);
          g_idle_add(emit_changed, obj);
          g_idle_add((GSourceFunc)emit_info, obj);
          obj->active = 1;

          if(obj->idle_timeout)
            obj->idle_timeout =
                g_timeout_add_seconds(300,
                                      (GSourceFunc)idle_timeout_cb,
                                      obj);
        }
      }
    }
  }
  else
    log_error("Couldn't create the proxy object",
              "Unknown(dbus_g_proxy_new_for_name)", 0);
}

static void
platform_soc_audio_logicaldev_input_cb(DBusGProxy *proxy,
                                       const gchar *condition,
                                       const gchar *details,
                                       FmtxObject *obj)
{
  DBusGProxy *hal_proxy;
  GError *error = NULL;
  GPtrArray *array = NULL;

  hal_proxy =
      dbus_g_proxy_new_for_name(
        obj->dbus,
        "org.freedesktop.Hal",
        "/org/freedesktop/Hal/devices/platform_soc_audio_logicaldev_input",
        "org.freedesktop.Hal.Device");

  if ( !hal_proxy )
  {
    log_error("Couldn't create the proxy object",
              "Unknown(dbus_g_proxy_new_for_name)", 0);
    return;
  }

  dbus_g_proxy_call(hal_proxy, "GetPropertyString", &error,
                    G_TYPE_STRING, "input.jack.type", 0,
                    dbus_g_type_get_collection("GPtrArray", G_TYPE_STRING),
                    &array, 0);

  g_object_unref(hal_proxy);

  if(error)
  {
    log_error("Unable to get headphone connector state", "", 0);
    g_clear_error(&error);
    return;
  }

  if(array->len)
  {
    obj->hp_connected = TRUE;

     if(g_str_equal(obj->state, "enabled"))
    {
      g_signal_emit(obj, FMTX_OBJECT_GET_CLASS(obj)->error, 0,
                    "fmtx_ni_cable_error");
      fmtx_enable(obj, 0);
      g_idle_add(emit_changed, obj);
      g_idle_add((GSourceFunc)emit_info, obj);

     obj->active = TRUE;

     if (!obj->idle_timeout)
        obj->idle_timeout = g_timeout_add_seconds(300,
                                                  (GSourceFunc)idle_timeout_cb,
                                                  obj);
    }
  }
  else
  {
    obj->hp_connected = 0;

    if ( obj->active && !obj->usb_connected && !obj->call_active )
    {
      if(obj->idle_timeout)
      {
        g_source_remove(obj->idle_timeout);
        obj->idle_timeout = 0;
      }

      obj->active = FALSE;
      fmtx_enable(obj, 1);
      g_idle_add(emit_changed, obj);
      g_idle_add((GSourceFunc)emit_info, obj);
    }
  }

  g_ptr_array_free(array, 1);
}

static void
g_cclosure_user_marshal_VOID__STRING_STRING (GClosure * closure,
					     GValue * return_value,
					     guint n_param_values,
					     const GValue * param_values,
					     gpointer invocation_hint,
					     gpointer marshal_data) {
	typedef void (*GMarshalFunc_VOID__STRING_STRING) (gpointer data1,
							  const char* arg_1,
							  const char* arg_2,
							  gpointer data2);
	register GMarshalFunc_VOID__STRING_STRING callback;
	register GCClosure * cc;
	register gpointer data1;
	register gpointer data2;
	cc = (GCClosure *) closure;
	g_return_if_fail (n_param_values == 3);
	if (G_CCLOSURE_SWAP_DATA (closure)) {
		data1 = closure->data;
		data2 = param_values->data[0].v_pointer;
	} else {
		data1 = param_values->data[0].v_pointer;
		data2 = closure->data;
	}
	callback = (GMarshalFunc_VOID__STRING_STRING)
	    (marshal_data ? marshal_data : cc->callback);
	callback (data1, g_value_get_string (param_values + 1),
		  g_value_get_string (param_values + 2), data2);
}

static void
sig_call_state_ind_cb(DBusGProxy *proxy, const gchar *call_state,
                      const gchar *call_e_state, FmtxObject *obj)
{
  if(g_str_equal(MCE_CALL_STATE_ACTIVE, call_state))
  {
    obj->call_active = TRUE;
    if(g_str_equal(obj->state, "enabled"))
    {
      fmtx_enable(obj, FALSE);
      g_idle_add(emit_changed, obj);
      g_idle_add((GSourceFunc)emit_info, obj);
      obj->active = TRUE;
      if(!obj->idle_timeout)
        obj->idle_timeout = g_timeout_add_seconds(300u,
                                                  (GSourceFunc)idle_timeout_cb,
                                                  obj);
    }
  }
  else
  {
    obj->call_active = FALSE;
    if(obj->active && !obj->usb_connected && !obj->hp_connected)
    {
      if(obj->idle_timeout)
      {
        g_source_remove(obj->idle_timeout);
        obj->idle_timeout = 0;
      }
      obj->active = FALSE;
      fmtx_enable(obj, TRUE);
      g_idle_add(emit_changed, obj);
      g_idle_add((GSourceFunc)emit_info, obj);
    }
  }
}

void
connect_dbus_signals(DBusGConnection *dbus, FmtxObject *obj)
{
  DBusGProxy *proxy;
  gchar *s;
  GError *err = NULL;

  err = 0;
  proxy = dbus_g_proxy_new_for_name(
        dbus,
        "org.freedesktop.Hal",
        "/org/freedesktop/Hal/devices/platform_soc_audio_logicaldev_input",
        "org.freedesktop.Hal.Device");

  if(!proxy)
    goto err;

  dbus_g_proxy_add_signal(proxy, "Condition",
                          G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
  dbus_g_proxy_connect_signal(proxy, "Condition",
                              (GCallback)platform_soc_audio_logicaldev_input_cb,
                              obj, NULL);
  platform_soc_audio_logicaldev_input_cb(0, 0, 0, obj);

  proxy = dbus_g_proxy_new_for_name(dbus,
                                    MCE_SERVICE,
                                    MCE_SIGNAL_PATH,
                                    MCE_SIGNAL_IF);
  if(!proxy)
    goto err;

  dbus_g_proxy_add_signal(proxy, MCE_DEVICE_MODE_SIG,
                          G_TYPE_STRING, G_TYPE_INVALID);
  dbus_g_proxy_connect_signal(proxy, MCE_DEVICE_MODE_SIG,
                              (GCallback)sig_device_mode_ind_cb,
                              obj, NULL);
  dbus_g_object_register_marshaller(
        (GClosureMarshal)g_cclosure_user_marshal_VOID__STRING_STRING,
        G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING,G_TYPE_INVALID);
  dbus_g_proxy_add_signal(proxy, MCE_CALL_STATE_SIG,
                          G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
  dbus_g_proxy_connect_signal(proxy, MCE_CALL_STATE_SIG,
                              (GCallback)sig_call_state_ind_cb, obj, NULL);

  proxy = dbus_g_proxy_new_for_name(dbus,
                                    MCE_SERVICE,
                                    MCE_REQUEST_PATH,
                                    MCE_REQUEST_IF);
  if(!proxy)
    goto err;

  dbus_g_proxy_call(proxy, MCE_DEVICE_MODE_GET, &err,
                    G_TYPE_STRING, MCE_REQUEST_IF, G_TYPE_INVALID,
                    G_TYPE_STRING, &s, G_TYPE_INVALID);

  if(err)
  {
    log_error("Unable to get device state", "", 0);
    g_clear_error(&err);
    return;
  }
  if (!g_str_equal("normal", s))
    obj->offline = TRUE;
  g_free(s);

  proxy = dbus_g_proxy_new_for_name(
         dbus,
         "org.freedesktop.Hal",
         "/org/freedesktop/Hal/devices/usb_device_1d6b_2_musb_hdrc",
         "org.freedesktop.Hal.Device");

  if(!proxy)
    goto err;

  dbus_g_proxy_add_signal(proxy, "Condition",
                          G_TYPE_STRING, G_TYPE_STRING,
                          G_TYPE_INVALID);
  dbus_g_proxy_connect_signal(proxy, "Condition",
                              (GCallback)usb_device_1d6b_2_musb_hdrc_cb,
                              obj, NULL);
  usb_device_1d6b_2_musb_hdrc_cb(0, 0, 0, obj);

  return;

err:
    log_error("Couldn't create the proxy object",
              "Unknown(dbus_g_proxy_new_for_name)", FALSE);

    g_free(obj->state);
    obj->state = g_strdup("error");
}
