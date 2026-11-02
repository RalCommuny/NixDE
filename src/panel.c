#include "panel/panel.h"

#include <unistd.h>

#define PANEL_HEIGHT 44

static void launch_terminal(GtkButton *button, gpointer data) {
	(void)button;
	(void)data;
	char *argv[] = {"foot", NULL};
	GError *error = NULL;
	if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
			NULL, NULL, NULL, &error)) {
		g_warning("Could not start foot: %s", error->message);
		g_error_free(error);
	}
}

static void activate(GtkApplication *application, gpointer data) {
	NixdePanel *panel = data;
	panel->control_socket = g_strdup(g_getenv("NIXDE_CONTROL_SOCKET"));
	if (panel->control_socket == NULL) {
		g_warning("NIXDE_CONTROL_SOCKET is not set; task list unavailable");
	}

	GtkWidget *window = gtk_application_window_new(application);
	gtk_window_set_title(GTK_WINDOW(window), "NixDE Panel");
	gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
	gtk_window_set_deletable(GTK_WINDOW(window), FALSE);
	gtk_window_set_default_size(GTK_WINDOW(window), 800, PANEL_HEIGHT);
	gtk_widget_add_css_class(window, "nixde-panel");

	GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_add_css_class(bar, "nixde-panel-content");
	gtk_widget_set_margin_start(bar, 8);
	gtk_widget_set_margin_end(bar, 8);
	gtk_widget_set_margin_top(bar, 4);
	gtk_widget_set_margin_bottom(bar, 4);
	gtk_window_set_child(GTK_WINDOW(window), bar);

	gtk_box_append(GTK_BOX(bar), panel_create_launcher(panel));

	GtkWidget *terminal = gtk_button_new_with_label("Terminal");
	gtk_widget_add_css_class(terminal, "nixde-terminal-button");
	g_signal_connect(terminal, "clicked", G_CALLBACK(launch_terminal), NULL);
	gtk_box_append(GTK_BOX(bar), terminal);
	gtk_box_append(GTK_BOX(bar), panel_create_tasks(panel));
	gtk_box_append(GTK_BOX(bar), panel_create_clock());
	gtk_box_append(GTK_BOX(bar), panel_create_system_menu());

	panel_load_styles(panel, gdk_display_get_default());
	gtk_window_present(GTK_WINDOW(window));
	g_timeout_add(500, panel_refresh_tasks, panel);
	panel_refresh_tasks(panel);
}

static void panel_state_clear(NixdePanel *panel) {
	panel_clear_styles(panel);
	g_clear_pointer(&panel->control_socket, g_free);
	g_clear_pointer(&panel->previous_window_state, g_free);
}

int main(int argc, char **argv) {
	NixdePanel panel = {0};
	GtkApplication *application = gtk_application_new(
		"org.nixde.Panel", G_APPLICATION_NON_UNIQUE);
	g_signal_connect(application, "activate", G_CALLBACK(activate), &panel);
	int status = g_application_run(G_APPLICATION(application), argc, argv);
	panel_state_clear(&panel);
	g_object_unref(application);
	return status;
}
