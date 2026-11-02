#include "panel.h"

static gboolean app_row_filter(GtkListBoxRow *row, gpointer data) {
	(void)data;
	NixdePanel *panel = g_object_get_data(G_OBJECT(row), "nixde-panel");
	GAppInfo *app = g_object_get_data(G_OBJECT(row), "nixde-app-info");
	const char *query = gtk_editable_get_text(GTK_EDITABLE(panel->app_search));
	if (query == NULL || query[0] == '\0') {
		return TRUE;
	}
	char *folded_query = g_utf8_casefold(query, -1);
	char *folded_name = g_utf8_casefold(g_app_info_get_display_name(app), -1);
	gboolean matches = strstr(folded_name, folded_query) != NULL;
	g_free(folded_query);
	g_free(folded_name);
	return matches;
}

static void app_search_changed(GtkEditable *editable, gpointer data) {
	(void)editable;
	NixdePanel *panel = data;
	gtk_list_box_invalidate_filter(panel->app_list);
}

static void app_row_activated(GtkListBox *list, GtkListBoxRow *row,
		gpointer data) {
	(void)list;
	NixdePanel *panel = data;
	GAppInfo *app = g_object_get_data(G_OBJECT(row), "nixde-app-info");
	GError *error = NULL;
	if (!g_app_info_launch(app, NULL, NULL, &error)) {
		g_warning("Could not launch %s: %s",
			g_app_info_get_display_name(app), error->message);
		g_error_free(error);
		return;
	}
	gtk_popover_popdown(panel->apps_popover);
}

static gint compare_apps(gconstpointer left, gconstpointer right) {
	GAppInfo *left_app = (GAppInfo *)left;
	GAppInfo *right_app = (GAppInfo *)right;
	return g_utf8_collate(g_app_info_get_display_name(left_app),
		g_app_info_get_display_name(right_app));
}

static GtkWidget *create_app_list(NixdePanel *panel) {
	panel->app_list = GTK_LIST_BOX(gtk_list_box_new());
	gtk_widget_add_css_class(GTK_WIDGET(panel->app_list), "nixde-app-list");
	gtk_list_box_set_selection_mode(panel->app_list, GTK_SELECTION_NONE);
	gtk_list_box_set_filter_func(panel->app_list, app_row_filter, NULL, NULL);
	g_signal_connect(panel->app_list, "row-activated",
		G_CALLBACK(app_row_activated), panel);

	GList *apps = g_app_info_get_all();
	apps = g_list_sort(apps, compare_apps);
	for (GList *item = apps; item != NULL; item = item->next) {
		GAppInfo *app = item->data;
		if (!g_app_info_should_show(app)) {
			continue;
		}

		GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_widget_set_margin_start(row_box, 8);
		gtk_widget_set_margin_end(row_box, 8);
		gtk_widget_set_margin_top(row_box, 4);
		gtk_widget_set_margin_bottom(row_box, 4);
		GIcon *icon = g_app_info_get_icon(app);
		GtkWidget *image = icon != NULL
			? gtk_image_new_from_gicon(icon) : gtk_image_new();
		gtk_image_set_pixel_size(GTK_IMAGE(image), 22);
		gtk_box_append(GTK_BOX(row_box), image);

		GtkWidget *name = gtk_label_new(g_app_info_get_display_name(app));
		gtk_label_set_xalign(GTK_LABEL(name), 0);
		gtk_widget_set_hexpand(name, TRUE);
		gtk_box_append(GTK_BOX(row_box), name);

		GtkWidget *row = gtk_list_box_row_new();
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
		g_object_set_data(G_OBJECT(row), "nixde-panel", panel);
		g_object_set_data_full(G_OBJECT(row), "nixde-app-info",
			g_object_ref(app), g_object_unref);
		gtk_list_box_append(panel->app_list, row);
	}
	g_list_free_full(apps, g_object_unref);

	GtkWidget *scroller = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request(scroller, 320, 360);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller),
		GTK_WIDGET(panel->app_list));
	return scroller;
}

static GtkWidget *create_app_popover(NixdePanel *panel) {
	GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_add_css_class(content, "nixde-launcher");
	gtk_widget_set_size_request(content, 336, -1);
	gtk_widget_set_margin_start(content, 8);
	gtk_widget_set_margin_end(content, 8);
	gtk_widget_set_margin_top(content, 8);
	gtk_widget_set_margin_bottom(content, 8);

	panel->app_search = gtk_search_entry_new();
	gtk_widget_add_css_class(panel->app_search, "nixde-launcher-search");
	gtk_search_entry_set_placeholder_text(
		GTK_SEARCH_ENTRY(panel->app_search), "Buscar aplicaciones");
	gtk_box_append(GTK_BOX(content), panel->app_search);
	g_signal_connect(panel->app_search, "search-changed",
		G_CALLBACK(app_search_changed), panel);
	gtk_box_append(GTK_BOX(content), create_app_list(panel));
	return content;
}

GtkWidget *panel_create_launcher(NixdePanel *panel) {
	GtkWidget *button = gtk_menu_button_new();
	gtk_widget_add_css_class(button, "nixde-launcher-button");
	GtkWidget *label = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);

	char *logo_path = g_build_filename(NIXDE_DATADIR,
		"nixde-logo.svg", NULL);
	if (!g_file_test(logo_path, G_FILE_TEST_EXISTS)) {
		g_free(logo_path);
		logo_path = g_build_filename("data", "nixde-logo.svg", NULL);
	}
	GtkWidget *logo = gtk_image_new_from_file(logo_path);
	gtk_image_set_pixel_size(GTK_IMAGE(logo), 24);
	gtk_widget_add_css_class(logo, "nixde-brand-icon");
	gtk_box_append(GTK_BOX(label), logo);
	GtkWidget *name = gtk_label_new("NixDE");
	gtk_widget_add_css_class(name, "nixde-brand-name");
	gtk_box_append(GTK_BOX(label), name);
	g_free(logo_path);
	gtk_menu_button_set_child(GTK_MENU_BUTTON(button), label);

	panel->apps_popover = GTK_POPOVER(gtk_popover_new());
	gtk_widget_add_css_class(GTK_WIDGET(panel->apps_popover),
		"nixde-launcher-popover");
	gtk_popover_set_position(panel->apps_popover, GTK_POS_TOP);
	gtk_popover_set_child(panel->apps_popover, create_app_popover(panel));
	gtk_menu_button_set_popover(GTK_MENU_BUTTON(button),
		GTK_WIDGET(panel->apps_popover));
	return button;
}
