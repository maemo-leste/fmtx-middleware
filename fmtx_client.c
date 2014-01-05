#include <glib.h>
#include <dbus/dbus-glib.h>
#include <stdlib.h>
#include <unistd.h>

static void
print_error(const char *err_msg, const char *err_detail, gboolean quit)
{
  g_printerr("fmtx_client: ERROR: %s (%s)\n", err_msg, err_detail);

  if(quit)
    exit(1);
}

static void
set_property_value(DBusGProxy *proxy,
                   const char* property,
                   GValue *value,
                   const char *err_detail)
{
  GError *error = NULL;

  dbus_g_proxy_call(proxy, "Set",&error,
                    G_TYPE_STRING, "org.freedesktop.DBus.Properties",
                    G_TYPE_STRING, property,
                    G_TYPE_VALUE, value,
                    G_TYPE_INVALID, G_TYPE_INVALID);
  if(error)
  {
    print_error(error->message, err_detail, FALSE);
    g_clear_error(&error);
  }

  g_value_unset(value);
}

static void
show_usage()
{
  g_print("Usage:\n"
          "------\n"
          "-f<uint>\tSet frequency (in kHz)\n"
          "-s<string>\tSet RDS station name\n"
          "-t<string>\tSet RDS info text\n"
          "-p<uint>\tTurn fmtx on (1) or off (0)\n\n");
}

int main(int argc, char **argv)
{
  DBusGConnection *dbus;
  int opt, i;
  DBusGProxy *proxy;

  const char *const properties[]=
  {
    "version",
    "frequency",
    "freq_max",
    "freq_min",
    "freq_step",
    "state",
    "startable",
    "rds_ps",
    "rds_text"
  };

  GValue value = {0,};
  GError *error = NULL;

  g_type_init();
  show_usage();

  dbus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
  if ( error )
    print_error("Couldn't connect to the System bus", error->message, TRUE);

  proxy = dbus_g_proxy_new_for_name(dbus,
                                    "com.nokia.FMTx",
                                    "/com/nokia/fmtx/default",
                                    "org.freedesktop.DBus.Properties");
  if(!proxy)
    print_error("Couldn't create the proxy object",
                "Unknown(dbus_g_proxy_new_for_name)",
                TRUE);
  while(1)
  {
    opt = getopt(argc, argv, "f:s:t:p:");

    if ( opt == -1 )
      break;

    if(opt == 'p')
    {
      g_value_init(&value, G_TYPE_STRING);
      if(*optarg == '1')
        g_value_set_string(&value, "enabled");
      else
        g_value_set_string(&value, "disabled");
      set_property_value(proxy, "state", &value,
                         "Unable to set FmTx state");
    }
    else if(opt == 'f')
    {
      g_value_init(&value, G_TYPE_UINT);
      g_value_set_uint(&value, strtol(optarg, NULL, 10));
      set_property_value(proxy, "frequency", &value,
                         "Unable to set frequency");
    }
    else if(opt == 's')
    {
      g_value_init(&value, G_TYPE_STRING);
      g_value_set_string(&value, optarg);
      set_property_value(proxy, "rds_ps", &value,
                         "Unable to set RDS station name");
    }
    else if(opt == 't')
    {
      g_value_init(&value, G_TYPE_STRING);
      g_value_set_string(&value, optarg);
      set_property_value(proxy, "rds_text", &value,
                         "Unable to set RDS info text");
    }
    else
      print_error("Error in commandline arguments", "", TRUE);
  }

  g_print("Current settings (Frequencies in kHz):\n--------------------------------------\n");

  for(i = 0; i < sizeof(properties) / sizeof(const char*); i++)
  {
    gchar *s;

    dbus_g_proxy_call(proxy, "Get", &error,
                      G_TYPE_STRING, "org.freedesktop.DBus.Properties",
                      G_TYPE_STRING, properties[i],
                      G_TYPE_INVALID,
                      G_TYPE_VALUE, &value,
                      G_TYPE_INVALID);

    if(error)
    {
      print_error("Unable to get property", error->message, 0);
      g_clear_error(&error);
      continue;
    }

    if(G_VALUE_TYPE(&value) == G_TYPE_STRING ||
       G_VALUE_HOLDS(&value, G_TYPE_STRING))
      s = g_strdup(g_value_get_string(&value));

    if(G_VALUE_TYPE(&value) == G_TYPE_UINT ||
       G_VALUE_HOLDS(&value, G_TYPE_UINT) )
    {
      s = (gchar *)g_malloc(10);
      g_snprintf(s, 10, "%i", g_value_get_uint(&value));
    }

    g_print("%s=%s\n", properties[i], s);
    g_value_unset(&value);
    g_free(s);
  }

  return 0;
}
