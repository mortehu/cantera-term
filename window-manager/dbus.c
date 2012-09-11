#include <string.h>
#include <stdio.h>

#include <X11/Xlib.h>

#include <dbus/dbus.h>

#include "common.h"
#include "tree.h"

extern struct tree* config;

pid_t launch(const char* command, Time when);

static DBusConnection *dbus;
static DBusError error;

void
dbus_init ()
{
  dbus = dbus_bus_get (DBUS_BUS_SYSTEM , &error);

  dbus_bus_add_match (dbus, "type='signal',interface='org.cantera'", &error);

  if (dbus_error_is_set(&error))
    {
      /* XXX: unref dbus */
      dbus = 0;
    }

  dbus_connection_flush (dbus);
}

void
dbus_poll ()
{
  DBusMessage *msg;

  if (!dbus)
    return;

  dbus_connection_read_write (dbus, 0);

  while (0 != (msg = dbus_connection_pop_message (dbus)))
    {
      if (!strcmp ("org.cantera", dbus_message_get_interface (msg)))
        {
          char config_key[128];
          const char *command;

          snprintf (config_key, sizeof (config_key),
                    "dbus.%s", dbus_message_get_member (msg));

          if (0 != (command = tree_get_string_default (config, config_key, 0)))
            launch (command, 0);
        }

      dbus_message_unref (msg);
    }
}
