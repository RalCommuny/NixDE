#include "panel.h"

#ifndef NIXDE_DATADIR
#define NIXDE_DATADIR "/usr/share/nixde"
#endif

static void stylesheet_parse_error(GtkCssProvider *provider,
		GtkCssSection *section, const GError *error, gpointer data) {
	(void)provider;
	(void)section;
	(void)data;
	g_warning("NixDE panel CSS: %s", error->message);
}

static void load_provider(GtkCssProvider *provider, const char *path) {
	if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
		return;
	}
	GFile *file = g_file_new_for_path(path);
	gtk_css_provider_load_from_file(provider, file);
	g_object_unref(file);
}

static void reload_custom_css(NixdePanel *panel) {
	char *path = g_build_filename(g_get_user_config_dir(),
		"nixde", "panel.css", NULL);
	GFile *file = g_file_new_for_path(path);
	if (g_file_query_exists(file, NULL)) {
		gtk_css_provider_load_from_file(panel->custom_css, file);
	} else {
		gtk_css_provider_load_from_string(panel->custom_css, "");
	}
	g_object_unref(file);
	g_free(path);
}

static void css_file_changed(GFileMonitor *monitor, GFile *file,
		GFile *other_file, GFileMonitorEvent event, gpointer data) {
	(void)monitor;
	(void)file;
	(void)other_file;
	if (event == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT ||
			event == G_FILE_MONITOR_EVENT_CREATED ||
			event == G_FILE_MONITOR_EVENT_DELETED ||
			event == G_FILE_MONITOR_EVENT_MOVED_IN ||
			event == G_FILE_MONITOR_EVENT_MOVED_OUT ||
			event == G_FILE_MONITOR_EVENT_RENAMED) {
		reload_custom_css(data);
	}
}

void panel_load_styles(NixdePanel *panel, GdkDisplay *display) {
	panel->default_css = gtk_css_provider_new();
	panel->custom_css = gtk_css_provider_new();
	g_signal_connect(panel->default_css, "parsing-error",
		G_CALLBACK(stylesheet_parse_error), NULL);
	g_signal_connect(panel->custom_css, "parsing-error",
		G_CALLBACK(stylesheet_parse_error), NULL);

	char *default_path = g_build_filename(NIXDE_DATADIR,
		"panel.css", NULL);
	load_provider(panel->default_css, default_path);
	g_free(default_path);
	gtk_style_context_add_provider_for_display(display,
		GTK_STYLE_PROVIDER(panel->default_css),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	gtk_style_context_add_provider_for_display(display,
		GTK_STYLE_PROVIDER(panel->custom_css),
		GTK_STYLE_PROVIDER_PRIORITY_USER);

	char *config_dir = g_build_filename(g_get_user_config_dir(),
		"nixde", NULL);
	if (g_mkdir_with_parents(config_dir, 0700) != 0) {
		g_warning("Could not create NixDE config directory: %s", config_dir);
	}
	char *custom_path = g_build_filename(config_dir, "panel.css", NULL);
	reload_custom_css(panel);
	GFile *file = g_file_new_for_path(config_dir);
	panel->css_monitor = g_file_monitor_directory(file,
		G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
	if (panel->css_monitor != NULL) {
		g_signal_connect(panel->css_monitor, "changed",
			G_CALLBACK(css_file_changed), panel);
	} else {
		g_warning("Could not monitor NixDE panel CSS directory: %s", config_dir);
	}
	g_object_unref(file);
	g_free(custom_path);
	g_free(config_dir);
}

void panel_clear_styles(NixdePanel *panel) {
	if (panel->css_monitor != NULL) {
		g_file_monitor_cancel(panel->css_monitor);
		g_clear_object(&panel->css_monitor);
	}
	if (panel->default_css != NULL) {
		g_object_unref(panel->default_css);
	}
	if (panel->custom_css != NULL) {
		g_object_unref(panel->custom_css);
	}
}
