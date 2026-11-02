#include "panel.h"

static void focus_window(GtkButton *button, gpointer data) {
	NixdePanel *panel = g_object_get_data(G_OBJECT(button), "nixde-panel");
	guint id = GPOINTER_TO_UINT(data);
	char *request = g_strdup_printf("ACTIVATE %u\n", id);
	char *response = NULL;
	if (!panel_control_request(panel, request, &response)) {
		g_warning("Could not communicate with the NixDE compositor");
	}
	g_free(response);
	g_free(request);
}

static void close_window(GtkButton *button, gpointer data) {
	NixdePanel *panel = g_object_get_data(G_OBJECT(button), "nixde-panel");
	guint id = GPOINTER_TO_UINT(data);
	char *request = g_strdup_printf("CLOSE %u\n", id);
	if (!panel_control_request(panel, request, NULL)) {
		g_warning("Could not communicate with the NixDE compositor");
	}
	g_free(request);
}

static void task_context_menu(GtkGestureClick *gesture, int presses,
		double x, double y, gpointer data) {
	(void)presses;
	(void)x;
	(void)y;
	GtkWidget *task_button = data;
	NixdePanel *panel = g_object_get_data(G_OBJECT(task_button), "nixde-panel");
	guint id = GPOINTER_TO_UINT(
		g_object_get_data(G_OBJECT(task_button), "nixde-window-id"));

	GtkWidget *menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_add_css_class(menu, "nixde-task-context-menu");
	gtk_widget_set_margin_start(menu, 6);
	gtk_widget_set_margin_end(menu, 6);
	gtk_widget_set_margin_top(menu, 6);
	gtk_widget_set_margin_bottom(menu, 6);
	GtkWidget *close = gtk_button_new_with_label("Cerrar ventana");
	gtk_widget_add_css_class(close, "nixde-close-window-button");
	g_object_set_data(G_OBJECT(close), "nixde-panel", panel);
	g_signal_connect(close, "clicked", G_CALLBACK(close_window),
		GUINT_TO_POINTER(id));
	gtk_box_append(GTK_BOX(menu), close);

	GtkWidget *popover = gtk_popover_new();
	gtk_widget_add_css_class(popover, "nixde-task-context-popover");
	gtk_popover_set_child(GTK_POPOVER(popover), menu);
	gtk_widget_set_parent(popover, task_button);
	g_signal_connect_swapped(popover, "closed",
		G_CALLBACK(gtk_widget_unparent), popover);
	gtk_popover_set_pointing_to(GTK_POPOVER(popover),
		&(const GdkRectangle){.x = 0, .y = 0, .width = 1, .height = 1});
	gtk_popover_popup(GTK_POPOVER(popover));
	(void)gesture;
}

static char *desktop_id_stem(const char *id) {
	if (id == NULL) {
		return g_strdup("");
	}
	char *stem = g_strdup(id);
	char *suffix = g_strrstr(stem, ".desktop");
	if (suffix != NULL && suffix[8] == '\0') {
		*suffix = '\0';
	}
	return stem;
}

static GIcon *find_window_icon(const char *app_id) {
	char *wanted = desktop_id_stem(app_id);
	char *short_name = g_strdup(wanted);
	char *last_dot = strrchr(short_name, '.');
	if (last_dot != NULL) {
		memmove(short_name, last_dot + 1, strlen(last_dot + 1) + 1);
	}

	GList *apps = g_app_info_get_all();
	GIcon *icon = NULL;
	for (GList *item = apps; item != NULL; item = item->next) {
		GAppInfo *app = item->data;
		char *candidate = desktop_id_stem(g_app_info_get_id(app));
		const char *app_name = g_app_info_get_name(app);
		gboolean matches = g_ascii_strcasecmp(candidate, wanted) == 0 ||
			g_ascii_strcasecmp(candidate, short_name) == 0 ||
			(app_name != NULL &&
				(g_ascii_strcasecmp(app_name, wanted) == 0 ||
				g_ascii_strcasecmp(app_name, short_name) == 0));
		g_free(candidate);
		if (matches && g_app_info_get_icon(app) != NULL) {
			icon = g_object_ref(g_app_info_get_icon(app));
			break;
		}
	}
	g_list_free_full(apps, g_object_unref);

	if (icon == NULL && wanted[0] != '\0') {
		icon = g_themed_icon_new(wanted);
	}
	if (icon == NULL && short_name[0] != '\0') {
		icon = g_themed_icon_new(short_name);
	}
	if (icon == NULL) {
		icon = g_themed_icon_new("application-x-executable");
	}
	g_free(wanted);
	g_free(short_name);
	return icon;
}

static void clear_taskbar(GtkWidget *taskbar) {
	GtkWidget *child = gtk_widget_get_first_child(taskbar);
	while (child != NULL) {
		GtkWidget *next = gtk_widget_get_next_sibling(child);
		gtk_box_remove(GTK_BOX(taskbar), child);
		child = next;
	}
}

GtkWidget *panel_create_tasks(NixdePanel *panel) {
	GtkWidget *scroller = gtk_scrolled_window_new();
	gtk_widget_set_hexpand(scroller, TRUE);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
	panel->taskbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_widget_add_css_class(panel->taskbar, "nixde-task-list");
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller),
		panel->taskbar);
	return scroller;
}

gboolean panel_refresh_tasks(gpointer data) {
	NixdePanel *panel = data;
	char *response = NULL;
	if (!panel_control_request(panel, "LIST\n", &response)) {
		return G_SOURCE_CONTINUE;
	}
	if (g_strcmp0(response, panel->previous_window_state) == 0) {
		g_free(response);
		return G_SOURCE_CONTINUE;
	}
	g_free(panel->previous_window_state);
	panel->previous_window_state = g_strdup(response);
	clear_taskbar(panel->taskbar);

	char **lines = g_strsplit(response, "\n", -1);
	for (gsize i = 0; lines[i] != NULL; i++) {
		char **fields = g_strsplit(lines[i], "\t", 4);
		if (g_strv_length(fields) != 4 || fields[0][0] == '\0') {
			g_strfreev(fields);
			continue;
		}
		char *end = NULL;
		guint64 id = g_ascii_strtoull(fields[2], &end, 10);
		if (end == fields[2] || *end != '\0' || id == 0 || id > G_MAXUINT) {
			g_strfreev(fields);
			continue;
		}

		GtkWidget *button = gtk_button_new();
		g_object_set_data(G_OBJECT(button), "nixde-panel", panel);
		gtk_widget_add_css_class(button, "nixde-task-button");
		g_object_set_data(G_OBJECT(button), "nixde-window-id",
			GUINT_TO_POINTER((guint)id));
		if (g_str_equal(fields[3], "1")) {
			gtk_widget_add_css_class(button, "nixde-task-button-active");
		}

		GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
		GIcon *icon = find_window_icon(fields[1]);
		GtkWidget *image = gtk_image_new_from_gicon(icon);
		g_object_unref(icon);
		gtk_image_set_pixel_size(GTK_IMAGE(image), 20);
		gtk_box_append(GTK_BOX(content), image);

		GtkWidget *title = gtk_label_new(fields[0]);
		gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars(GTK_LABEL(title), 22);
		gtk_box_append(GTK_BOX(content), title);
		gtk_button_set_child(GTK_BUTTON(button), content);
		g_signal_connect(button, "clicked", G_CALLBACK(focus_window),
			GUINT_TO_POINTER((guint)id));
		GtkGesture *context_gesture = gtk_gesture_click_new();
		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(context_gesture),
			GDK_BUTTON_SECONDARY);
		g_signal_connect(context_gesture, "pressed",
			G_CALLBACK(task_context_menu), button);
		gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(context_gesture));
		gtk_box_append(GTK_BOX(panel->taskbar), button);
		g_strfreev(fields);
	}
	g_strfreev(lines);
	g_free(response);
	return G_SOURCE_CONTINUE;
}
