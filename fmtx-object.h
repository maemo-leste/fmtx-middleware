#ifndef __FMTX_H_INCLUDED__
#define __FMTX_H_INCLUDED__

#include <alsa/asoundlib.h>
#include <dbus/dbus-glib.h>
#include <gconf/gconf-client.h>
#include <glib.h>
#include <pulse/pulseaudio.h>

#define FMTX_MAX_RDS_TEXT 64
#define FMTX_SYSFS_NODE "/sys/bus/i2c/devices/2-0063/"

#define WRITE_FMTX_SYSFS(file, val, err_msg) \
  { \
    int fd = open(FMTX_SYSFS_NODE file, O_WRONLY); \
    if (fd == -1) \
      perror("fmtxd Error opening " file " file"); \
    else \
    { \
      if (write(fd, val, sizeof(val)) == -1) \
        perror(err_msg); \
      close(fd); \
    } \
  }

#define FMTX_OBJECT_TYPE (fmtx_object_get_type())

#define FMTX_OBJECT(obj) G_CHECK_CAST(obj, fmtx_object_get_type(), FmtxObject)
#define FMTX_OBJECT_CLASS(klass) \
  G_TYPE_CHECK_CLASS_CAST(klass, fmtx_object_get_type(), FmtxObjectClass)
#define FMTX_OBJECT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), FMTX_OBJECT_TYPE, FmtxObjectClass))

typedef struct _FmtxObject FmtxObject;
typedef struct _FmtxObjectClass FmtxObjectClass;

struct _FmtxObject
{
  GObject parent;
  DBusGConnection *dbus;
  GConfClient *gcclient;
  int power_level;
  int max_power_level;
  int frequency;
  unsigned int freq_max;
  unsigned int freq_min;
  unsigned int freq_step;
  char *state;
  char *rds_ps;
  char *rds_text;
  gboolean offline;
  gboolean hp_connected;
  gboolean pa_running;
  gboolean call_active;
  int dev_radio;
  gboolean mixer_inited;
  snd_mixer_elem_t *mixer_elem;
  int exit_timeout;
  snd_mixer_t *snd_mixer;
  pa_context *context;
  pa_mainloop_api *api;
  gboolean active;
  int idle_timeout;
  int pilot_timeout;
};

struct _FmtxObjectClass
{
  GObjectClass parent;
  int changed;
  int error;
  int info;
};

GType
fmtx_object_get_type();
gboolean
emit_changed(gpointer obj);
gboolean
emit_info(gpointer obj);
void
exit_timeout_cb(FmtxObject *obj);
int
fmtx_enable(FmtxObject *fmtx, gboolean enable);
int
fmtx_set_frequency(FmtxObject *fmtx, unsigned int frequency);
int
fmtx_set_rds_station_name(FmtxObject *obj, const char *rds_ps);
int
fmtx_set_rds_text(FmtxObject *obj, const char *rds_text);

void
log_error(const char *msg, const char *reason, gboolean quit);

#endif /* __FMTX_H_INCLUDED__ */
