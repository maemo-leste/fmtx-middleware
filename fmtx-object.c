#include "fmtx-object.h"
#include <glib.h>

G_DEFINE_TYPE(FmtxObject, fmtx_object, G_TYPE_OBJECT);

gboolean
emit_changed(gpointer obj)
{
  g_signal_emit(obj, FMTX_OBJECT_GET_CLASS(obj)->changed, 0);
  return FALSE;
}

gboolean
emit_info(gpointer obj)
{
  gchar *strv[2];
  gchar *s;

  s = g_strdup("fmtx");

  strv[0] = s;
  strv[1] = 0;

  g_signal_emit(obj,
                FMTX_OBJECT_GET_CLASS(obj)->info,
                0,
                "connected",
                g_str_equal(((FmtxObject *)obj)->state, "enabled") ? "1" : "0",
                strv);

  g_free(s);

  return 0;
}

void
exit_timeout_cb(FmtxObject *obj)
{
  snd_mixer_close(obj->snd_mixer);
  pa_context_disconnect(obj->context);
  g_object_unref(obj->gcclient);
  dbus_g_connection_unref(obj->dbus);
  exit(0);
}

int
fmtx_set_rds_text(FmtxObject *obj, const char *rds_text)
{
  int rv = 0;
  int fd;

  if (rds_text && (strlen(rds_text) <= FMTX_MAX_RDS_TEXT))
  {
    fd = open(FMTX_SYSFS_NODE "rds_radio_text", O_WRONLY);

    if (fd == -1)
    {
      perror("fmtxd Could not open rds text file");
      rv = 1;
    }
    else
    {
      if (write(fd, rds_text, strlen(rds_text) + 1) == -1)
      {
        perror("fmtxd Could not set rds info text");
        close(fd);
        rv = 1;
      }
      else
      {
        close(fd);
        g_free(obj->rds_text);
        obj->rds_text = g_strdup(rds_text);
        rv = 2;
      }
    }
  }

  return rv;
}

int
fmtx_set_rds_station_name(FmtxObject *obj, const char *rds_ps)
{
  int fd;
  size_t i;
  char buf[9];

  if (!rds_ps)
    return 0;

  strncpy(buf, rds_ps, sizeof(buf) - 1);

  if ((i = strlen(rds_ps)) < sizeof(buf) - 1)
  {
    char *p = &buf[i];

    do
    {
      *p++ = ' ';
    }
    while (p != &buf[sizeof(buf) - 1]);
  }

  buf[sizeof(buf) - 1] = 0;

  fd = open(FMTX_SYSFS_NODE "rds_ps_name", O_WRONLY);

  if (fd == -1)
  {
    perror("fmtxd Could not open rds station name file");
    return 1;
  }

  if (write(fd, buf, sizeof(buf)) == -1)
  {
    perror("fmtxd Could not set rds station name");
    close(fd);
    return 1;
  }

  close(fd);
  g_free(obj->rds_ps);
  obj->rds_ps = g_strdup(rds_ps);

  return 2;
}

static gboolean
dbus_glib_marshal_fmtx_object_get(FmtxObject *obj,
                                  gconstpointer iname,
                                  gconstpointer pname,
                                  GValue *value,
                                  GError **error)
{
  gboolean rv = FALSE;
  gboolean startable;
  GValue v = { 0, };

  if (obj->exit_timeout)
  {
    g_source_remove(obj->exit_timeout);
    obj->exit_timeout = 0;
  }

  if (g_str_equal(pname, "version"))
  {
    g_value_init(&v, G_TYPE_UINT);
    g_value_set_uint(&v, 1u);
    rv = TRUE;
  }

  if (g_str_equal(pname, "frequency"))
  {
    g_value_init(&v, G_TYPE_UINT);
    g_value_set_uint(&v, obj->frequency);
    rv = TRUE;
  }

  if (g_str_equal(pname, "freq_max"))
  {
    g_value_init(&v, G_TYPE_UINT);
    g_value_set_uint(&v, obj->freq_max);
    rv = TRUE;
  }

  if (g_str_equal(pname, "freq_min"))
  {
    g_value_init(&v, G_TYPE_UINT);
    g_value_set_uint(&v, obj->freq_min);
    rv = TRUE;
  }

  if (g_str_equal(pname, "freq_step"))
  {
    g_value_init(&v, G_TYPE_UINT);
    g_value_set_uint(&v, obj->freq_step);
    rv = TRUE;
  }

  if (g_str_equal(pname, "startable"))
  {
    g_value_init(&v, G_TYPE_STRING);

    if (obj->offline)
    {
      g_value_set_string(&v, "Device is in offline mode");
      startable = FALSE;
    }
    else
      startable = TRUE;

    if (obj->hp_connected)
    {
      g_value_set_string(&v, "Headphones are connected");
      startable = 0;
    }

    if (startable)
      g_value_set_string(&v, "true");

    rv = TRUE;
  }

  if (g_str_equal(pname, "state"))
  {
    g_value_init(&v, G_TYPE_STRING);
    g_value_set_string(&v, obj->state);
    rv = TRUE;
  }

  if (g_str_equal(pname, "rds_ps"))
  {
    g_value_init(&v, G_TYPE_STRING);
    g_value_set_string(&v, obj->rds_ps);
    rv = TRUE;
  }

  if (g_str_equal(pname, "rds_text"))
  {
    g_value_init(&v, G_TYPE_STRING);
    g_value_set_string(&v, obj->rds_text);
    rv = TRUE;
  }

  if (!g_str_equal(obj->state, "enabled") && !obj->active)
    obj->exit_timeout = g_timeout_add(60000, (GSourceFunc)exit_timeout_cb, obj);

  if (rv)
    memcpy(value, &v, sizeof(v));
  else
    g_set_error(error, DBUS_GERROR, DBUS_GERROR_INVALID_ARGS,
                "Property does not exist");

  return rv;
}

static gboolean
dbus_glib_marshal_fmtx_object_set(FmtxObject *obj,
                                  gconstpointer iname,
                                  gconstpointer pname,
                                  GValue *value,
                                  GError **error)
{
  gboolean property_found = FALSE;
  gboolean rv = FALSE;

  if (obj->exit_timeout)
  {
    g_source_remove(obj->exit_timeout);
    obj->exit_timeout = 0;
  }

  if (g_str_equal(pname, "version") ||
      g_str_equal(pname, "frequency_min") ||
      g_str_equal(pname, "frequency_max") ||
      g_str_equal(pname, "frequency_step"))
  {
    g_set_error(error, DBUS_GERROR, DBUS_GERROR_ACCESS_DENIED,
                "Property is read only");
    property_found = TRUE;
  }

  if (g_str_equal(pname, "frequency"))
  {
    unsigned int f = g_value_get_uint(value);
    int tmp = fmtx_set_frequency(obj, f);

    if (tmp == 2)
    {
      g_idle_add(emit_changed, obj);
      rv = TRUE;
    }
    else if (tmp == 1)
      g_set_error(error, DBUS_GERROR, DBUS_GERROR_FAILED,
                  "Frequency could not be set");
    else if (!tmp)
      g_set_error(error, DBUS_GERROR, DBUS_GERROR_INVALID_ARGS,
                  "Frequency is not currently allowed");
    else
      g_set_error(error, DBUS_GERROR, DBUS_GERROR_FAILED,
                  "Unknown return code");

    property_found = TRUE;
  }

  if (g_str_equal(pname, "state"))
  {
    int res = 0;

    if (g_str_equal(obj->state, "error"))
      g_set_error(error, DBUS_GERROR, DBUS_GERROR_FAILED,
                  "Device initialization failed");

    if (g_str_equal(g_value_get_string(value), "enabled"))
    {
      if (obj->offline)
        g_set_error(error, DBUS_GERROR, DBUS_GERROR_FAILED,
                    "Device is in offline mode");

      if (obj->hp_connected)
        g_set_error(error, DBUS_GERROR, DBUS_GERROR_FAILED,
                    "Headphones are connected");

      res = fmtx_enable(obj, TRUE);
    }
    else
    {
      if (g_str_equal(g_value_get_string(value), "disabled"))
      {
        res = fmtx_enable(obj, FALSE);

        if (obj->pilot_timeout)
        {
          g_source_remove(obj->pilot_timeout);
          obj->pilot_timeout = 0;
        }
      }
      else
        g_set_error(error, DBUS_GERROR, DBUS_GERROR_INVALID_ARGS,
                    "Unknown state");
    }

    if (res == 2)
    {
      g_idle_add(emit_changed, obj);
      g_idle_add((GSourceFunc)emit_info, obj);
      rv = TRUE;
    }
    else
      g_set_error(error, DBUS_GERROR, DBUS_GERROR_FAILED,
                  "Failed to change fmtx state");

    property_found = TRUE;
  }

  if (g_str_equal(pname, "rds_ps"))
  {
    int res = fmtx_set_rds_station_name(obj, g_value_get_string(value));

    if (res == 2)
    {
      g_idle_add(emit_changed, obj);
      rv = TRUE;
    }
    else if (res == 1)
      g_set_error(error, DBUS_GERROR, DBUS_GERROR_FAILED,
                  "RDS station name could not be set");
    else if (!res)
      g_set_error(error, DBUS_GERROR, DBUS_GERROR_INVALID_ARGS,
                  "Invalid RDS station name");
    else
      g_set_error(error, DBUS_GERROR, DBUS_GERROR_FAILED,
                  "Unknown return code");

    property_found = TRUE;
  }

  if (g_str_equal(pname, "rds_text"))
  {
    int res = fmtx_set_rds_text(obj, g_value_get_string(value));

    if (res == 2)
    {
      g_idle_add(emit_changed, obj);
      rv = TRUE;
    }
    else if (res == 1)
      g_set_error(error, DBUS_GERROR, DBUS_GERROR_FAILED,
                  "RDS text could not be set");
    else if (!res)
      g_set_error(error, DBUS_GERROR, DBUS_GERROR_INVALID_ARGS,
                  "Invalid RDS text");
    else
      g_set_error(error, DBUS_GERROR, DBUS_GERROR_FAILED,
                  "Unknown return code");

    property_found = TRUE;
  }

  if (!g_str_equal(obj->state, "enabled") && !obj->active)
    obj->exit_timeout = g_timeout_add(60000, (GSourceFunc)exit_timeout_cb, obj);

  if (!property_found)
    g_set_error(error, DBUS_GERROR, DBUS_GERROR_INVALID_ARGS,
                "Property does not exist");

  return rv;
}

static gboolean
dbus_glib_marshal_fmtx_object_get_all(FmtxObject *obj,
                                      gconstpointer iname,
                                      GArray *array,
                                      GError **error)
{
  return FALSE;
}

#include "fmtx-object-bindings.h"

static void
fmtx_object_init(FmtxObject *obj)
{
  g_assert(obj != NULL);

  obj->gcclient = 0;
  obj->power_level = 0;
  obj->max_power_level = 0;
  obj->frequency = 0;
  obj->freq_max = 0;
  obj->freq_min = 0;
  obj->freq_step = 100;
  obj->dev_radio = -1;
  obj->offline = FALSE;
  obj->call_active = FALSE;
  obj->hp_connected = FALSE;
  obj->state = g_strdup("initializing");
  obj->rds_ps = g_strdup("");
  obj->rds_text = g_strdup("");
  obj->mixer_inited = FALSE;
  obj->pa_running = FALSE;
  obj->mixer_elem = 0;
  obj->active = FALSE;
  obj->idle_timeout = 0;
  obj->pilot_timeout = 0;
}

static void
fmtx_object_class_init(FmtxObjectClass *klass)
{
  klass->changed = g_signal_new(
      "changed",
      G_OBJECT_CLASS_TYPE(klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  klass->error = g_signal_new(
      "error",
      G_OBJECT_CLASS_TYPE(klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__STRING,
      G_TYPE_NONE, 1,
      G_TYPE_STRING);

  klass->info = g_signal_new(
      "info",
      G_OBJECT_CLASS_TYPE(klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      dbus_glib_marshal_fmtx_object_BOOLEAN__STRING_STRING_BOXED_POINTER,
      G_TYPE_NONE, 3,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRV);

  dbus_g_object_type_install_info(FMTX_OBJECT_TYPE,
                                  &dbus_glib_fmtx_object_object_info);
}
