#include <cal.h>
#include <errno.h>
#include <glib/gprintf.h>
#include <libintl.h>
#include <linux/videodev.h>
#include <locale.h>
#include <string.h>
#include <sys/ioctl.h>

#include "audio.h"
#include "dbus.h"
#include "fmtx-object.h"

struct cal_fmtx_power_level
{
  char standard[8];
  unsigned int level;
};

static int
fmtx_set_preemphasis_level(FmtxObject *fmtx, int level)
{
  int fd;
  int rv;
  char buf[10];

  g_snprintf(buf, sizeof(buf), "%u", level);
  fd = open(FMTX_SYSFS_NODE "region_preemphasis", O_WRONLY);

  if (fd == -1)
  {
    perror("fmtxd Could not open pre-emphasis file");
    rv = 1;
  }
  else
  {
    if (write(fd, buf, strlen(buf) + 1) == -1)
    {
      perror("fmtxd Could not set FM tx pre-emphasis level");
      close(fd);
      rv = 1;
    }
    else
    {
      close(fd);
      rv = 2;
    }
  }

  return rv;
}

static int
fmtx_get_cal_power_level(const char *standard)
{
  void *fmtx_pwl;
  struct cal *cal;
  unsigned long len;
  struct cal_fmtx_power_level pl[3];

  if (cal_init(&cal) < 0)
    return -1;

  if (cal_read_block(cal, "fmtx_pwl", &fmtx_pwl, &len, 0) < 0)
  {
    g_log(0, G_LOG_LEVEL_WARNING, "CAL: failed to read fmtx_pwl from cal\n");
    cal_finish(cal);
    return 0;
  }

  memcpy(pl, fmtx_pwl, len > sizeof(pl) ? sizeof(pl) : len);
  free(fmtx_pwl);
  cal_finish(cal);

  /* WTF did Nokia developer do here, why is CAL std ignored? */
  if (!memcmp(standard, "fcc", 3))
    return pl[0].level;

  if (!memcmp(standard, "etsi", 4))
    return pl[1].level;

  if (!memcmp(standard, "anfr", 4))
    return pl[2].level;

  g_log(0, G_LOG_LEVEL_WARNING, "FMTX: Invalid standard\n");

  return 0;
}

static int
fmtx_set_power_level(FmtxObject *obj, int level)
{
  char buf[10];
  int fd;

  if (obj->max_power_level < level)
    return 0;

  if (level < 88)
    level = 88;

  g_snprintf(buf, sizeof(buf), "%u", level);

  fd = open(FMTX_SYSFS_NODE "power_level", O_WRONLY);

  if (fd == -1)
  {
    g_log(0, G_LOG_LEVEL_WARNING,
          "fmtxd Could not open fmtx power level file: %s", strerror(errno));
    return 1;
  }

  if (write(fd, buf, strlen(buf) + 1) == -1)
  {
    g_log(0, G_LOG_LEVEL_WARNING,
          "fmtxd Could not set FM tx power level: %s", strerror(errno));
    close(fd);
    return 1;
  }

  close(fd);
  obj->power_level = level;
  return 2;
}

void
log_error(const char *msg, const char *reason, gboolean quit)
{
  g_printerr("fmtxd: ERROR: %s (%s)\n", msg, reason);

  if (quit)
    exit(1);
}

static gboolean
nameownerchanged_cb(FmtxObject *obj, gconstpointer pname)
{
  gboolean rv = g_str_equal(pname, "org.freedesktop.ohm");

  if (rv)
    rv = emit_info(obj);

  return rv;
}

static int
fmtx_init(FmtxObject *obj)
{
  int fd;
  int i;
  unsigned int f;
  DBusGProxy *proxy;
  const char *err_msg;
  char file[50];
  GError *error = NULL;
  GArray *array = NULL;
  GError *err = NULL;

  fd = open(FMTX_SYSFS_NODE "pilot_frequency", O_WRONLY);

  if (fd == -1)
  {
    perror("fmtxd Could not open pilot tone frequency file");
    return 1;
  }

  if (write(fd, "19000", 6) == -1)
  {
    err_msg = "fmtxd Could not set pilot tone frequency";
    goto err_out;
  }

  close(fd);

  fd = open(FMTX_SYSFS_NODE "pilot_enabled", O_WRONLY);

  if (fd == -1)
  {
    perror("fmtxd Could not open pilot enable file");
    return 1;
  }

  /* FIXME Why 1, but not 2??? */
  if (write(fd, "1", 1) == -1)
  {
    err_msg = "fmtxd Could not set pilot tone";
    goto err_out;
  }

  close(fd);

  fd = open(FMTX_SYSFS_NODE "rds_pi", O_WRONLY);

  if (fd == -1)
  {
    perror("fmtxd Could not open RDS Program Information file");
    return 1;
  }

  /* FIXME - same here, no term zero written */
  if (write(fd, "6099", 4) == -1)
  {
    err_msg = "fmtxd Could not set RDS PI";
    goto err_out;
  }

  close(fd);

  i = 0;

  while (1)
  {
    snprintf(file, sizeof(file), "%s%i", "/dev/radio", i);
    fd = open(file, 0);
    obj->dev_radio = fd;

    if (fd > 0)
      break;

    i++;

    if (i == 2)
    {
      perror("fmtxd Could not open fmtx device");
      g_free(obj->state);
      obj->state = g_strdup("error");
      return 1;
    }
  }

  f = gconf_client_get_int(obj->gcclient, "/system/fmtx/frequency", &err);

  if (err)
  {
    g_fprintf(stderr, "Could not load fmtx settings: %s\n", err->message);
    return 1;
  }

  proxy = dbus_g_proxy_new_for_name(obj->dbus,
                                    "com.nokia.SystemInfo",
                                    "/com/nokia/SystemInfo",
                                    "com.nokia.SystemInfo");

  if (proxy)
  {
    if (dbus_g_proxy_call(proxy, "GetConfigValue", &error, G_TYPE_STRING,
                          "/certs/ccc/pp/fmtx-raw",
                          G_TYPE_INVALID,
                          DBUS_TYPE_G_UCHAR_ARRAY, &array,
                          G_TYPE_INVALID) && !error)
    {
      int std = *array->data;

      g_array_free(array, TRUE);
      g_object_unref(proxy);

      if (std == 2)
      {
        obj->max_power_level = fmtx_get_cal_power_level("etsi");
        fmtx_set_preemphasis_level(obj, 50);
        obj->freq_step = 100;
      }
      else if (std == 3)
      {
        obj->max_power_level = fmtx_get_cal_power_level("etsi");
        fmtx_set_preemphasis_level(obj, 75);
        obj->freq_step = 100;
      }
      else if (std == 4)
      {
        obj->max_power_level = fmtx_get_cal_power_level("fcc");
        fmtx_set_preemphasis_level(obj, 50);
        obj->freq_step = 200;
      }
      else if (std == 5)
      {
        obj->max_power_level = fmtx_get_cal_power_level("fcc");
        fmtx_set_preemphasis_level(obj, 75);
        obj->freq_step = 200;
      }
      else
        goto bad_std;

      obj->freq_min = 88100;
      obj->freq_max = 107900;
      goto set_power;
    }
    else
    {
      g_log(0, G_LOG_LEVEL_WARNING, "Unable to get stored fmtx settings");
      g_clear_error(&error);
    }
  }
  else
    g_log(0, G_LOG_LEVEL_WARNING, "Couldn't create the proxy object");

bad_std:
  obj->state = g_strdup("n/a");

set_power:
  fmtx_set_power_level(obj, obj->max_power_level);

  fmtx_set_rds_station_name(obj, "Nokia   ");

  fmtx_set_rds_text(obj, " ");

  fd = fmtx_set_frequency(obj, f);

  if (fd == 1)
    return 1;

  if (!fd)
    fmtx_set_frequency(obj, obj->freq_min);

  if (fmtx_enable(obj, 0) == 1)
    return 1;

  return 2;

err_out:
  perror(err_msg);
  close(fd);
  return 1;
}

static gboolean
check_mixer(FmtxObject *obj)
{
  gboolean old;
  unsigned int idxp;

  old = obj->mixer_inited;

  if (obj->mixer_elem)
  {
    snd_mixer_selem_get_enum_item(obj->mixer_elem,
                                  SND_MIXER_SCHN_FRONT_LEFT,
                                  &idxp);

    obj->mixer_inited = (idxp != 0);

    if (obj->mixer_inited != old)
      fmtx_toggle_pilot(obj);
  }

  return TRUE;
}

static void
mixer_init(FmtxObject *obj)
{
  snd_mixer_selem_id_t *sid;

  snd_mixer_elem_t *elem;

  char name[16];

  snd_mixer_selem_id_alloca(&sid);
  memset(sid, 0, snd_mixer_selem_id_sizeof());

  sprintf(name, "hw:%i", snd_card_get_index("0"));

  if (snd_mixer_open(&obj->snd_mixer, 0) < 0)
    log_error("snd_mixer_open", 0, 1);

  if (snd_mixer_attach(obj->snd_mixer, name) < 0)
    log_error("snd_mixer_attach", 0, 1);

  if (snd_mixer_selem_register(obj->snd_mixer, 0, 0) < 0)
    log_error("snd_mixer_selem_register", 0, 1);

  if (snd_mixer_load(obj->snd_mixer) < 0)
    log_error("snd_mixer_load", 0, 1);

  elem = snd_mixer_first_elem(obj->snd_mixer);

  if (elem)
  {
    while (1)
    {
      const char *s;
      snd_mixer_selem_get_id(elem, sid);
      s = snd_mixer_selem_id_get_name(sid);

      if (g_str_equal(s, "FMTX Function"))
        break;

      elem = snd_mixer_elem_next(elem);

      if (!elem)
        break;
    }

    if (elem)
      obj->mixer_elem = elem;
  }

  g_timeout_add(1000u, (GSourceFunc)check_mixer, obj);
}

int
main(int argc, char **argv)
{
  FILE *fp;
  FmtxObject *fmtx;
  DBusGConnection *dbus;
  DBusGProxy *proxy;
  GMainLoop *loop;
  char buf[100];
  GError *error = NULL;
  unsigned int ret;

  if ((argc > 1) && g_str_equal("-d", argv[1]) && (daemon(0, 0) == -1))
    log_error("Failed to daemonize", "Unknown(OOM?)", 1);

  bindtextdomain("osso-fm-transmitter", "/usr/share/locale");

  fp = fopen("/etc/osso-af-init/locale", "r");

  if (fp)
  {
    char *p;

    do
    {
      if (feof(fp))
      {
        fclose(fp);
        goto deflocale;
      }

      fgets(buf, sizeof(buf), fp);
      p = strstr(buf, "LANG=");
    }
    while (!p);

    *(p + strlen(p) - 1) = 0;

    setlocale(LC_ALL, p + 5);
    fclose(fp);
  }
  else
  {
deflocale:
    setlocale(LC_ALL, "en_GB");
  }

  g_setenv("DISPLAY", ":0.0", 0);

  fp = fopen("session_bus_address.user", "r");

  if (fp)
  {
    char *p;

    while (!feof(fp))
    {
      fgets(buf, 100, fp);
      p = strstr(buf, "DBUS_SESSION_BUS_ADDRESS='");

      if (p)
      {
        *(p + strlen(p) - 2) = 0;
        g_setenv("DBUS_SESSION_BUS_ADDRESS", p + 26, FALSE);
        break;
      }
    }

    fclose(fp);
  }

  g_type_init();

  fmtx = (FmtxObject *)g_object_new(FMTX_OBJECT_TYPE, 0);

  if (!fmtx)
    log_error("Failed to create one Value instance.", "Unknown(OOM?)", TRUE);

  loop = g_main_loop_new(NULL, FALSE);

  if (!loop)
    log_error("Couldn't create GMainLoop", "Unknown(OOM?)", TRUE);

  dbus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);

  if (error)
    log_error("Couldn't connect to the System bus", error->message, TRUE);

  proxy = dbus_g_proxy_new_for_name(dbus,
                                    "org.freedesktop.DBus",
                                    "/org/freedesktop/DBus",
                                    "org.freedesktop.DBus");

  if (!proxy)
    log_error("Failed to get a proxy for D-Bus",
              "Unknown(dbus_g_proxy_new_for_name)", TRUE);

  if (!dbus_g_proxy_call(proxy, "RequestName",
                         &error,
                         G_TYPE_STRING, "com.nokia.FMTx",
                         G_TYPE_UINT, DBUS_NAME_FLAG_DO_NOT_QUEUE,
                         G_TYPE_INVALID,
                         G_TYPE_UINT, &ret,
                         G_TYPE_INVALID ))
    log_error("D-Bus.RequestName RPC failed", error->message, TRUE);

  if (ret != 1)
    log_error("Failed to get the primary well-known name.",
              "RequestName result != 1", TRUE);

  fmtx->dbus = dbus;
  fmtx->gcclient = gconf_client_get_default();

  dbus_g_connection_register_g_object(dbus,
                                      "/com/nokia/fmtx/default",
                                      G_OBJECT(fmtx));

  connect_dbus_signals(dbus, fmtx);
  mixer_init(fmtx);
  register_pa(fmtx);

  dbus_g_proxy_add_signal(proxy, "NameOwnerChanged",
                          G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                          G_TYPE_INVALID);

  dbus_g_proxy_connect_signal(proxy,
                              "NameOwnerChanged",
                              (GCallback)nameownerchanged_cb,
                              fmtx,
                              G_TYPE_INVALID);
  g_object_unref(proxy);

  if (!g_str_equal(fmtx->state, "error") && (fmtx_init(fmtx) != 1))
  {
    emit_info(fmtx);

    if (!g_str_equal(fmtx->state, "enabled"))
      fmtx->exit_timeout = g_timeout_add(60000,
                                         (GSourceFunc)exit_timeout_cb,
                                         fmtx);

    g_main_loop_run(loop);
  }

  return 1;
}
