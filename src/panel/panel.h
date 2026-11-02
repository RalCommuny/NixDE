#ifndef NIXDE_PANEL_H
#define NIXDE_PANEL_H

#include <gtk/gtk.h>

typedef struct {
	GtkWidget *taskbar;
	GtkWidget *app_search;
	GtkListBox *app_list;
	GtkPopover *apps_popover;
	GtkCssProvider *default_css;
	GtkCssProvider *custom_css;
	GFileMonitor *css_monitor;
	char *control_socket;
	char *previous_window_state;
} NixdePanel;

gboolean panel_control_request(NixdePanel *panel, const char *request,
	char **response_out);
GtkWidget *panel_create_launcher(NixdePanel *panel);
GtkWidget *panel_create_tasks(NixdePanel *panel);
gboolean panel_refresh_tasks(gpointer data);
GtkWidget *panel_create_clock(void);
GtkWidget *panel_create_system_menu(void);
void panel_load_styles(NixdePanel *panel, GdkDisplay *display);
void panel_clear_styles(NixdePanel *panel);

#endif
