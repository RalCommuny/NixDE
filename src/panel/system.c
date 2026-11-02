#include "panel.h"

static void run_power_action(GtkButton *button, gpointer data) {
	(void)button;
	const char *action = data;
	char *argv[4] = {NULL};
	if (g_str_equal(action, "logout")) {
		const char *session = g_getenv("XDG_SESSION_ID");
		if (session == NULL || *session == '\0') {
			g_warning("XDG_SESSION_ID is not set; cannot log out through logind");
			return;
		}
		argv[0] = "loginctl";
		argv[1] = "terminate-session";
		argv[2] = (char *)session;
	} else {
		argv[0] = "systemctl";
		argv[1] = (char *)action;
	}

	GError *error = NULL;
	if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
			NULL, NULL, NULL, &error)) {
		g_warning("Could not run %s: %s", argv[0], error->message);
		g_error_free(error);
	}
}

static GtkWidget *make_action_button(const char *label, const char *action) {
	GtkWidget *button = gtk_button_new_with_label(label);
	g_signal_connect(button, "clicked", G_CALLBACK(run_power_action),
		(gpointer)action);
	return button;
}

static gboolean update_clock(gpointer data) {
	GtkLabel *label = GTK_LABEL(data);
	GDateTime *now = g_date_time_new_now_local();
	char *text = g_date_time_format(now, "%a %d %b  %H:%M");
	gtk_label_set_text(label, text);
	g_free(text);
	g_date_time_unref(now);
	return G_SOURCE_CONTINUE;
}

GtkWidget *panel_create_clock(void) {
	GtkWidget *clock = gtk_label_new("");
	gtk_widget_add_css_class(clock, "nixde-clock");
	update_clock(clock);
	g_timeout_add_seconds(1, update_clock, clock);
	return clock;
}

GtkWidget *panel_create_system_menu(void) {
	GtkWidget *button = gtk_menu_button_new();
	gtk_widget_add_css_class(button, "nixde-system-button");
	gtk_menu_button_set_label(GTK_MENU_BUTTON(button), "Sistema");

	GtkWidget *menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_add_css_class(menu, "nixde-system-menu");
	gtk_widget_set_margin_start(menu, 8);
	gtk_widget_set_margin_end(menu, 8);
	gtk_widget_set_margin_top(menu, 8);
	gtk_widget_set_margin_bottom(menu, 8);
	gtk_box_append(GTK_BOX(menu), make_action_button("Cerrar sesión", "logout"));
	gtk_box_append(GTK_BOX(menu), make_action_button("Reiniciar", "reboot"));
	gtk_box_append(GTK_BOX(menu), make_action_button("Apagar", "poweroff"));

	GtkWidget *popover = gtk_popover_new();
	gtk_widget_add_css_class(popover, "nixde-system-popover");
	gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_TOP);
	gtk_popover_set_child(GTK_POPOVER(popover), menu);
	gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), popover);
	return button;
}
