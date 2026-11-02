#define _GNU_SOURCE

#include <errno.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <sys/un.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

extern char **environ;

#define PANEL_APP_ID "org.nixde.Panel"
#define PANEL_HEIGHT 44

struct nixde_server;

struct nixde_toplevel {
	struct wl_list link;
	struct nixde_server *server;
	uint32_t id;
	struct wlr_xdg_toplevel *xdg;
	struct wlr_scene_tree *tree;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	bool is_panel;
	bool mapped;
};

struct nixde_keyboard {
	struct wl_list link;
	struct nixde_server *server;
	struct wlr_keyboard *keyboard;
	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
	uint32_t swallowed_keycode;
};

struct nixde_output {
	struct nixde_server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener destroy;
};

struct nixde_popup {
	struct wlr_xdg_popup *xdg;
	struct wl_listener commit;
	struct wl_listener destroy;
};

struct nixde_ipc_client {
	struct nixde_server *server;
	struct wl_event_source *source;
	int fd;
	char request[96];
	size_t request_size;
};

struct nixde_server {
	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	struct wlr_output_layout *output_layout;
	struct wlr_xdg_shell *xdg_shell;
	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_manager;
	struct wlr_seat *seat;
	struct wl_list keyboards;
	struct wl_list toplevels;
	struct nixde_toplevel *active;
	struct nixde_toplevel *panel;
	struct nixde_toplevel *moving;
	pid_t panel_pid;
	uint32_t next_toplevel_id;
	int ipc_fd;
	struct wl_event_source *ipc_source;
	char ipc_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	double move_offset_x;
	double move_offset_y;
	struct wl_listener new_output;
	struct wl_listener new_input;
	struct wl_listener new_toplevel;
	struct wl_listener new_popup;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;
	struct wl_listener request_cursor;
	struct wl_listener request_selection;
	struct wl_listener output_layout_change;
};

static bool spawn_program(const char *program, char *const argv[]) {
	pid_t child;
	int error = posix_spawnp(&child, program, NULL, NULL, argv, environ);
	if (error != 0) {
		wlr_log(WLR_ERROR, "could not start %s: %s", program, strerror(error));
		return false;
	}
	return true;
}

static void raise_panel(struct nixde_server *server) {
	if (server->panel != NULL && server->panel->mapped) {
		wlr_scene_node_raise_to_top(&server->panel->tree->node);
	}
}

static bool is_panel_app_id(const char *app_id) {
	return app_id != NULL && strcmp(app_id, PANEL_APP_ID) == 0;
}

static void focus_toplevel(struct nixde_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->is_panel || !toplevel->mapped) {
		return;
	}

	struct nixde_server *server = toplevel->server;
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	struct wlr_surface *surface = toplevel->xdg->base->surface;

	if (server->active != toplevel) {
		if (server->active != NULL) {
			wlr_xdg_toplevel_set_activated(server->active->xdg, false);
		}
		wlr_xdg_toplevel_set_activated(toplevel->xdg, true);
	}
	server->active = toplevel;

	wlr_scene_node_raise_to_top(&toplevel->tree->node);
	wl_list_remove(&toplevel->link);
	wl_list_insert(&server->toplevels, &toplevel->link);
	raise_panel(server);

	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(server->seat, surface,
			keyboard->keycodes, keyboard->num_keycodes,
			&keyboard->modifiers);
	}
}

static struct nixde_toplevel *focused_toplevel(struct nixde_server *server) {
	return server->active;
}

static void focus_next_toplevel(struct nixde_server *server) {
	if (wl_list_empty(&server->toplevels) ||
			wl_list_length(&server->toplevels) < 2) {
		return;
	}
	struct nixde_toplevel *next =
		wl_container_of(server->toplevels.prev, next, link);
	focus_toplevel(next);
}

static void ipc_client_close(struct nixde_ipc_client *client) {
	wl_event_source_remove(client->source);
	close(client->fd);
	free(client);
}

static void ipc_send(struct nixde_ipc_client *client, const char *message,
		size_t length) {
	size_t sent = 0;
	while (sent < length) {
		ssize_t result = send(client->fd, message + sent, length - sent,
			MSG_NOSIGNAL);
		if (result <= 0) {
			return;
		}
		sent += (size_t)result;
	}
}

static void ipc_send_windows(struct nixde_ipc_client *client) {
	struct nixde_server *server = client->server;
	struct nixde_toplevel *active = focused_toplevel(server);
	struct nixde_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (!toplevel->mapped || toplevel->is_panel) {
			continue;
		}
		const char *title = toplevel->xdg->title;
		if (title == NULL || title[0] == '\0') {
			title = toplevel->xdg->app_id;
		}
		if (title == NULL || title[0] == '\0') {
			title = "Ventana";
		}
		char line[1024];
		size_t title_length = 0;
		while (title[title_length] != '\0' && title_length < 640) {
			char character = title[title_length];
			line[title_length] = (character == '\t' || character == '\n' ||
				character == '\r') ? ' ' : character;
			title_length++;
		}
		line[title_length] = '\0';
		char app_id[160] = {0};
		const char *raw_app_id = toplevel->xdg->app_id;
		if (raw_app_id != NULL) {
			size_t app_id_length = 0;
			while (raw_app_id[app_id_length] != '\0' &&
					app_id_length < sizeof(app_id) - 1) {
				char character = raw_app_id[app_id_length];
				app_id[app_id_length] =
					(character == '\t' || character == '\n' ||
					character == '\r') ? '_' : character;
				app_id_length++;
			}
		}
		int length = snprintf(line + title_length, sizeof(line) - title_length,
			"\t%s\t%u\t%u\n", app_id, toplevel->id, toplevel == active);
		if (length > 0 && (size_t)length < sizeof(line) - title_length) {
			ipc_send(client, line, title_length + (size_t)length);
		}
	}
}

static void ipc_handle_window_action(struct nixde_server *server,
		const char *request, const char *prefix, bool close_window) {
	char *end = NULL;
	unsigned long requested_id = strtoul(request + strlen(prefix), &end, 10);
	if (end == request + strlen(prefix) || *end != '\0' ||
			requested_id > UINT32_MAX) {
		return;
	}
	struct nixde_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->id != (uint32_t)requested_id ||
				!toplevel->mapped || toplevel->is_panel) {
			continue;
		}
		if (close_window) {
			wlr_xdg_toplevel_send_close(toplevel->xdg);
		} else {
			focus_toplevel(toplevel);
		}
		return;
	}
}

static int ipc_client_ready(int fd, uint32_t mask, void *data) {
	struct nixde_ipc_client *client = data;
	if (mask & WL_EVENT_ERROR) {
		ipc_client_close(client);
		return 0;
	}

	ssize_t received = recv(fd, client->request + client->request_size,
		sizeof(client->request) - client->request_size - 1, 0);
	if (received <= 0) {
		ipc_client_close(client);
		return 0;
	}
	client->request_size += (size_t)received;
	client->request[client->request_size] = '\0';
	char *newline = strchr(client->request, '\n');
	if (newline == NULL && client->request_size < sizeof(client->request) - 1) {
		return 0;
	}
	if (newline != NULL) {
		*newline = '\0';
	}

	if (strcmp(client->request, "LIST") == 0) {
		ipc_send_windows(client);
	} else if (strncmp(client->request, "ACTIVATE ", 9) == 0) {
		ipc_handle_window_action(client->server, client->request,
			"ACTIVATE ", false);
	} else if (strncmp(client->request, "CLOSE ", 6) == 0) {
		ipc_handle_window_action(client->server, client->request,
			"CLOSE ", true);
	}
	ipc_client_close(client);
	return 0;
}

static int ipc_accept_ready(int fd, uint32_t mask, void *data) {
	struct nixde_server *server = data;
	if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
		return 0;
	}
	int client_fd = accept4(fd, NULL, NULL, SOCK_CLOEXEC);
	if (client_fd < 0) {
		return 0;
	}
	struct nixde_ipc_client *client = calloc(1, sizeof(*client));
	if (client == NULL) {
		close(client_fd);
		wlr_log(WLR_ERROR, "%s", "could not allocate IPC client");
		return 0;
	}
	client->server = server;
	client->fd = client_fd;
	client->source = wl_event_loop_add_fd(wl_display_get_event_loop(server->display),
		client_fd, WL_EVENT_READABLE, ipc_client_ready, client);
	if (client->source == NULL) {
		close(client_fd);
		free(client);
		wlr_log(WLR_ERROR, "%s", "could not register IPC client");
	}
	return 0;
}

static bool server_create_ipc(struct nixde_server *server) {
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (runtime_dir == NULL || runtime_dir[0] == '\0') {
		wlr_log(WLR_ERROR, "%s", "XDG_RUNTIME_DIR is not set");
		return false;
	}
	int length = snprintf(server->ipc_path, sizeof(server->ipc_path),
		"%s/nixde-%ld.sock", runtime_dir, (long)getpid());
	if (length < 0 || (size_t)length >= sizeof(server->ipc_path)) {
		wlr_log(WLR_ERROR, "%s", "NixDE IPC socket path is too long");
		return false;
	}
	server->ipc_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (server->ipc_fd < 0) {
		wlr_log(WLR_ERROR, "could not create NixDE IPC socket: %s",
			strerror(errno));
		return false;
	}
	struct sockaddr_un address = {.sun_family = AF_UNIX};
	strcpy(address.sun_path, server->ipc_path);
	unlink(server->ipc_path);
	if (bind(server->ipc_fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
			chmod(server->ipc_path, 0600) < 0 || listen(server->ipc_fd, 16) < 0) {
		wlr_log(WLR_ERROR, "could not listen on NixDE IPC socket: %s",
			strerror(errno));
		close(server->ipc_fd);
		server->ipc_fd = -1;
		unlink(server->ipc_path);
		return false;
	}
	server->ipc_source = wl_event_loop_add_fd(
		wl_display_get_event_loop(server->display), server->ipc_fd,
		WL_EVENT_READABLE, ipc_accept_ready, server);
	if (server->ipc_source == NULL) {
		wlr_log(WLR_ERROR, "%s", "could not register NixDE IPC socket");
		close(server->ipc_fd);
		server->ipc_fd = -1;
		unlink(server->ipc_path);
		return false;
	}
	return true;
}

static bool handle_keybinding(struct nixde_server *server,
		xkb_keysym_t symbol) {
	if (symbol == XKB_KEY_Return) {
		char *const argv[] = {"foot", NULL};
		spawn_program(argv[0], argv);
		return true;
	} else if (symbol == XKB_KEY_space) {
		char *const argv[] = {"fuzzel", NULL};
		spawn_program(argv[0], argv);
		return true;
	} else if (symbol == XKB_KEY_Tab) {
		focus_next_toplevel(server);
		return true;
	} else if (symbol == XKB_KEY_F4) {
		struct nixde_toplevel *toplevel = focused_toplevel(server);
		if (toplevel != NULL && !toplevel->is_panel && toplevel->mapped) {
			wlr_xdg_toplevel_send_close(toplevel->xdg);
			return true;
		}
	}
	return false;
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
	struct nixde_keyboard *keyboard =
		wl_container_of(listener, keyboard, modifiers);
	(void)data;
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->keyboard);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
		&keyboard->keyboard->modifiers);
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
	struct nixde_keyboard *keyboard =
		wl_container_of(listener, keyboard, key);
	struct wlr_keyboard_key_event *event = data;
	if (event->state == WL_KEYBOARD_KEY_STATE_RELEASED &&
			keyboard->swallowed_keycode == event->keycode) {
		keyboard->swallowed_keycode = 0;
		return;
	}

	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->keyboard);
	bool handled = false;

	if ((modifiers & WLR_MODIFIER_ALT) &&
			event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		const xkb_keysym_t *symbols;
		int count = xkb_state_key_get_syms(keyboard->keyboard->xkb_state,
			event->keycode + 8, &symbols);
		for (int i = 0; i < count; i++) {
			if (symbols[i] == XKB_KEY_q || symbols[i] == XKB_KEY_Q) {
				if (modifiers & WLR_MODIFIER_SHIFT) {
					wl_display_terminate(keyboard->server->display);
					handled = true;
				}
			} else {
				handled = handle_keybinding(keyboard->server, symbols[i]) ||
					handled;
			}
		}
	}

	if (handled) {
		keyboard->swallowed_keycode = event->keycode;
		return;
	}
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->keyboard);
	wlr_seat_keyboard_notify_key(keyboard->server->seat,
		event->time_msec, event->keycode, event->state);
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
	struct nixde_keyboard *keyboard =
		wl_container_of(listener, keyboard, destroy);
	(void)data;
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

static void server_new_keyboard(struct nixde_server *server,
		struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);
	struct nixde_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	if (keyboard == NULL) {
		wlr_log(WLR_ERROR, "%s", "could not allocate keyboard state");
		return;
	}
	keyboard->server = server;
	keyboard->keyboard = wlr_keyboard;

	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (context == NULL) {
		wlr_log(WLR_ERROR, "%s", "could not create XKB context");
		free(keyboard);
		return;
	}
	struct xkb_rule_names rules = {
		.layout = getenv("XKB_DEFAULT_LAYOUT"),
		.variant = getenv("XKB_DEFAULT_VARIANT"),
		.options = getenv("XKB_DEFAULT_OPTIONS"),
	};
	struct xkb_keymap *keymap =
		xkb_keymap_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
	xkb_context_unref(context);
	if (keymap == NULL) {
		wlr_log(WLR_ERROR, "%s", "could not create keyboard keymap");
		free(keyboard);
		return;
	}
	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);
	wlr_seat_set_keyboard(server->seat, wlr_keyboard);
	wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_input(struct wl_listener *listener, void *data) {
	struct nixde_server *server =
		wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		wlr_cursor_attach_input_device(server->cursor, device);
		break;
	default:
		break;
	}

	uint32_t capabilities = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		capabilities |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, capabilities);
}

static struct wlr_surface *surface_at(struct nixde_server *server,
		double x, double y, double *sx, double *sy) {
	struct wlr_scene_node *node =
		wlr_scene_node_at(&server->scene->tree.node, x, y, sx, sy);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(buffer);
	return scene_surface == NULL ? NULL : scene_surface->surface;
}

static void process_cursor_motion(struct nixde_server *server, uint32_t time) {
	if (server->moving != NULL) {
		wlr_scene_node_set_position(&server->moving->tree->node,
			(int)(server->cursor->x - server->move_offset_x),
			(int)(server->cursor->y - server->move_offset_y));
		raise_panel(server);
		return;
	}

	double sx, sy;
	struct wlr_surface *surface =
		surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
	if (surface == NULL) {
		wlr_seat_pointer_notify_clear_focus(server->seat);
		wlr_cursor_set_xcursor(server->cursor, server->cursor_manager, "default");
		return;
	}
	wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
	wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
}

static void cursor_handle_motion(struct wl_listener *listener, void *data) {
	struct nixde_server *server =
		wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	wlr_cursor_move(server->cursor, &event->pointer->base,
		event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

static void cursor_handle_motion_absolute(struct wl_listener *listener,
		void *data) {
	struct nixde_server *server =
		wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base,
		event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

static void cursor_handle_button(struct wl_listener *listener, void *data) {
	struct nixde_server *server =
		wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	uint32_t modifiers = keyboard == NULL ? 0 :
		wlr_keyboard_get_modifiers(keyboard);

	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED &&
			server->moving != NULL) {
		server->moving = NULL;
		return;
	}

	double sx, sy;
	struct wlr_surface *surface =
		surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
	if (event->state == WL_POINTER_BUTTON_STATE_PRESSED && surface != NULL) {
		struct wlr_xdg_toplevel *xdg =
			wlr_xdg_toplevel_try_from_wlr_surface(
				wlr_surface_get_root_surface(surface));
		struct nixde_toplevel *toplevel;
		wl_list_for_each(toplevel, &server->toplevels, link) {
			if (toplevel->xdg == xdg) {
				focus_toplevel(toplevel);
				break;
			}
		}
		if ((modifiers & WLR_MODIFIER_ALT) &&
				event->button == BTN_LEFT && xdg != NULL) {
			wl_list_for_each(toplevel, &server->toplevels, link) {
				if (toplevel->xdg == xdg && !toplevel->is_panel) {
					server->moving = toplevel;
					server->move_offset_x =
						server->cursor->x - toplevel->tree->node.x;
					server->move_offset_y =
						server->cursor->y - toplevel->tree->node.y;
					return;
				}
			}
		}
	}

	wlr_seat_pointer_notify_button(server->seat, event->time_msec,
		event->button, event->state);
}

static void cursor_handle_axis(struct wl_listener *listener, void *data) {
	struct nixde_server *server =
		wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
		event->orientation, event->delta, event->delta_discrete,
		event->source, event->relative_direction);
}

static void cursor_handle_frame(struct wl_listener *listener, void *data) {
	struct nixde_server *server =
		wl_container_of(listener, server, cursor_frame);
	(void)data;
	wlr_seat_pointer_notify_frame(server->seat);
}

static void seat_request_cursor(struct wl_listener *listener, void *data) {
	struct nixde_server *server =
		wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	if (server->seat->pointer_state.focused_client == event->seat_client) {
		wlr_cursor_set_surface(server->cursor, event->surface,
			event->hotspot_x, event->hotspot_y);
	}
}

static void seat_request_selection(struct wl_listener *listener, void *data) {
	struct nixde_server *server =
		wl_container_of(listener, server, request_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static void output_handle_frame(struct wl_listener *listener, void *data) {
	struct nixde_output *output = wl_container_of(listener, output, frame);
	(void)data;
	struct wlr_scene_output *scene_output =
		wlr_scene_get_scene_output(output->server->scene, output->wlr_output);
	if (scene_output == NULL ||
			!wlr_scene_output_commit(scene_output, NULL)) {
		return;
	}
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_handle_destroy(struct wl_listener *listener, void *data) {
	struct nixde_output *output =
		wl_container_of(listener, output, destroy);
	(void)data;
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->destroy.link);
	free(output);
}

static void panel_apply_geometry(struct nixde_server *server) {
	struct nixde_toplevel *panel = server->panel;
	if (panel == NULL || !panel->mapped) {
		return;
	}
	struct wlr_box box;
	wlr_output_layout_get_box(server->output_layout, NULL, &box);
	if (box.width <= 0 || box.height <= 0) {
		return;
	}
	wlr_scene_node_set_position(&panel->tree->node, box.x,
		box.y + box.height - PANEL_HEIGHT);
	if (panel->xdg->current.width != box.width ||
			panel->xdg->current.height != PANEL_HEIGHT) {
		wlr_xdg_toplevel_set_size(panel->xdg, box.width, PANEL_HEIGHT);
	}
	raise_panel(server);
}

static void output_layout_handle_change(struct wl_listener *listener,
		void *data) {
	struct nixde_server *server =
		wl_container_of(listener, server, output_layout_change);
	(void)data;
	panel_apply_geometry(server);
}

static void server_new_output(struct wl_listener *listener, void *data) {
	struct nixde_server *server =
		wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	wlr_output_init_render(wlr_output, server->allocator, server->renderer);
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL) {
		wlr_output_state_set_mode(&state, mode);
	}
	bool committed = wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);
	if (!committed) {
		wlr_log(WLR_ERROR, "could not enable output %s", wlr_output->name);
		return;
	}

	struct nixde_output *output = calloc(1, sizeof(*output));
	if (output == NULL) {
		wlr_log(WLR_ERROR, "%s", "could not allocate output state");
		return;
	}
	output->server = server;
	output->wlr_output = wlr_output;
	output->frame.notify = output_handle_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	output->destroy.notify = output_handle_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	struct wlr_output_layout_output *layout_output =
		wlr_output_layout_add_auto(server->output_layout, wlr_output);
	struct wlr_scene_output *scene_output =
		wlr_scene_output_create(server->scene, wlr_output);
	wlr_scene_output_layout_add_output(server->scene_layout,
		layout_output, scene_output);
	wlr_xcursor_manager_load(server->cursor_manager, wlr_output->scale);
	panel_apply_geometry(server);
}

static void toplevel_handle_map(struct wl_listener *listener, void *data) {
	struct nixde_toplevel *toplevel = wl_container_of(listener, toplevel, map);
	(void)data;
	struct nixde_server *server = toplevel->server;
	toplevel->is_panel = is_panel_app_id(toplevel->xdg->app_id);
	toplevel->mapped = true;

	if (toplevel->is_panel) {
		server->panel = toplevel;
		panel_apply_geometry(server);
		return;
	}

	int offset = 48 * (int)wl_list_length(&server->toplevels);
	wlr_scene_node_set_position(&toplevel->tree->node, 64 + offset,
		48 + offset);
	toplevel->id = ++server->next_toplevel_id;
	if (toplevel->id == 0) {
		toplevel->id = ++server->next_toplevel_id;
	}
	wl_list_insert(&server->toplevels, &toplevel->link);
	focus_toplevel(toplevel);
}

static void toplevel_handle_unmap(struct wl_listener *listener, void *data) {
	struct nixde_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
	(void)data;
	struct nixde_server *server = toplevel->server;
	if (toplevel->is_panel) {
		if (server->panel == toplevel) {
			server->panel = NULL;
		}
		toplevel->mapped = false;
		return;
	}
	bool was_focused = focused_toplevel(server) == toplevel;
	if (toplevel->mapped) {
		wl_list_remove(&toplevel->link);
		toplevel->mapped = false;
	}
	if (server->moving == toplevel) {
		server->moving = NULL;
	}
	if (was_focused) {
		server->active = NULL;
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		if (!wl_list_empty(&server->toplevels)) {
			struct nixde_toplevel *next =
				wl_container_of(server->toplevels.next, next, link);
			focus_toplevel(next);
		}
	}
}

static void toplevel_handle_commit(struct wl_listener *listener, void *data) {
	struct nixde_toplevel *toplevel =
		wl_container_of(listener, toplevel, commit);
	(void)data;
	if (!toplevel->mapped) {
		toplevel->is_panel = is_panel_app_id(toplevel->xdg->app_id);
	}
	if (!toplevel->xdg->base->initial_commit) {
		if (toplevel->is_panel) {
			panel_apply_geometry(toplevel->server);
		}
		return;
	}
	if (toplevel->is_panel) {
		panel_apply_geometry(toplevel->server);
	} else {
		wlr_xdg_toplevel_set_size(toplevel->xdg, 0, 0);
	}
}

static void popup_handle_commit(struct wl_listener *listener, void *data) {
	struct nixde_popup *popup = wl_container_of(listener, popup, commit);
	(void)data;
	if (popup->xdg->base->initial_commit) {
		wlr_xdg_surface_schedule_configure(popup->xdg->base);
	}
}

static void popup_handle_destroy(struct wl_listener *listener, void *data) {
	struct nixde_popup *popup = wl_container_of(listener, popup, destroy);
	(void)data;
	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);
	free(popup);
}

static void toplevel_handle_destroy(struct wl_listener *listener, void *data) {
	struct nixde_toplevel *toplevel =
		wl_container_of(listener, toplevel, destroy);
	(void)data;
	if (toplevel->mapped && !toplevel->is_panel) {
		wl_list_remove(&toplevel->link);
	}
	if (toplevel->server->panel == toplevel) {
		toplevel->server->panel = NULL;
	}
	bool was_active = toplevel->server->active == toplevel;
	if (was_active) {
		toplevel->server->active = NULL;
	}
	if (toplevel->server->moving == toplevel) {
		toplevel->server->moving = NULL;
	}
	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->destroy.link);
	if (was_active && !wl_list_empty(&toplevel->server->toplevels)) {
		struct nixde_toplevel *next =
			wl_container_of(toplevel->server->toplevels.next, next, link);
		focus_toplevel(next);
	}
	free(toplevel);
}

static void server_new_toplevel(struct wl_listener *listener, void *data) {
	struct nixde_server *server =
		wl_container_of(listener, server, new_toplevel);
	struct wlr_xdg_toplevel *xdg = data;
	struct nixde_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	if (toplevel == NULL) {
		wlr_log(WLR_ERROR, "%s", "could not allocate window state");
		return;
	}
	toplevel->server = server;
	toplevel->xdg = xdg;
	toplevel->is_panel = is_panel_app_id(xdg->app_id);
	toplevel->tree = wlr_scene_xdg_surface_create(
		&server->scene->tree, xdg->base);
	if (toplevel->tree == NULL) {
		wlr_log(WLR_ERROR, "%s", "could not create window scene node");
		free(toplevel);
		return;
	}
	xdg->base->data = toplevel->tree;

	toplevel->map.notify = toplevel_handle_map;
	wl_signal_add(&xdg->base->surface->events.map, &toplevel->map);
	toplevel->unmap.notify = toplevel_handle_unmap;
	wl_signal_add(&xdg->base->surface->events.unmap, &toplevel->unmap);
	toplevel->commit.notify = toplevel_handle_commit;
	wl_signal_add(&xdg->base->surface->events.commit, &toplevel->commit);
	toplevel->destroy.notify = toplevel_handle_destroy;
	wl_signal_add(&xdg->events.destroy, &toplevel->destroy);
}

static void server_new_popup(struct wl_listener *listener, void *data) {
	(void)listener;
	struct wlr_xdg_popup *xdg = data;
	struct wlr_xdg_surface *parent =
		wlr_xdg_surface_try_from_wlr_surface(xdg->parent);
	if (parent == NULL || parent->data == NULL) {
		wlr_log(WLR_ERROR, "%s", "popup has no managed parent surface");
		return;
	}

	struct nixde_popup *popup = calloc(1, sizeof(*popup));
	if (popup == NULL) {
		wlr_log(WLR_ERROR, "%s", "could not allocate popup state");
		return;
	}
	popup->xdg = xdg;
	xdg->base->data = wlr_scene_xdg_surface_create(parent->data, xdg->base);
	if (xdg->base->data == NULL) {
		wlr_log(WLR_ERROR, "%s", "could not create popup scene node");
		free(popup);
		return;
	}
	popup->commit.notify = popup_handle_commit;
	wl_signal_add(&xdg->base->surface->events.commit, &popup->commit);
	popup->destroy.notify = popup_handle_destroy;
	wl_signal_add(&xdg->events.destroy, &popup->destroy);
}

static int server_handle_signal(int signal_number, void *data) {
	struct nixde_server *server = data;
	(void)signal_number;
	wl_display_terminate(server->display);
	return 0;
}

static int server_reap_children(int signal_number, void *data) {
	struct nixde_server *server = data;
	(void)signal_number;
	pid_t child;
	while ((child = waitpid(-1, NULL, WNOHANG)) > 0) {
		if (child == server->panel_pid) {
			server->panel_pid = 0;
		}
	}
	return 0;
}

static bool initialize_server(struct nixde_server *server) {
	server->display = wl_display_create();
	if (server->display == NULL) {
		wlr_log(WLR_ERROR, "%s", "could not create Wayland display");
		return false;
	}
	struct wl_event_loop *loop = wl_display_get_event_loop(server->display);
	server->backend = wlr_backend_autocreate(loop, NULL);
	if (server->backend == NULL) {
		wlr_log(WLR_ERROR, "%s", "could not create wlroots backend");
		return false;
	}
	server->renderer = wlr_renderer_autocreate(server->backend);
	if (server->renderer == NULL) {
		wlr_log(WLR_ERROR, "%s", "could not create wlroots renderer");
		return false;
	}
	wlr_renderer_init_wl_display(server->renderer, server->display);
	server->allocator = wlr_allocator_autocreate(server->backend,
		server->renderer);
	if (server->allocator == NULL) {
		wlr_log(WLR_ERROR, "%s", "could not create wlroots allocator");
		return false;
	}

	wlr_compositor_create(server->display, 5, server->renderer);
	wlr_subcompositor_create(server->display);
	wlr_data_device_manager_create(server->display);
	server->output_layout = wlr_output_layout_create(server->display);
	server->scene = wlr_scene_create();
	if (server->output_layout == NULL || server->scene == NULL) {
		wlr_log(WLR_ERROR, "%s", "could not initialize output layout or scene");
		return false;
	}
	server->scene_layout =
		wlr_scene_attach_output_layout(server->scene, server->output_layout);
	server->xdg_shell = wlr_xdg_shell_create(server->display, 3);
	if (server->scene_layout == NULL || server->xdg_shell == NULL) {
		wlr_log(WLR_ERROR, "%s", "could not initialize compositor protocols");
		return false;
	}

	wl_list_init(&server->keyboards);
	wl_list_init(&server->toplevels);
	server->new_output.notify = server_new_output;
	wl_signal_add(&server->backend->events.new_output, &server->new_output);
	server->output_layout_change.notify = output_layout_handle_change;
	wl_signal_add(&server->output_layout->events.change,
		&server->output_layout_change);
	server->new_input.notify = server_new_input;
	wl_signal_add(&server->backend->events.new_input, &server->new_input);
	server->new_toplevel.notify = server_new_toplevel;
	wl_signal_add(&server->xdg_shell->events.new_toplevel,
		&server->new_toplevel);
	server->new_popup.notify = server_new_popup;
	wl_signal_add(&server->xdg_shell->events.new_popup, &server->new_popup);

	server->cursor = wlr_cursor_create();
	server->cursor_manager = wlr_xcursor_manager_create(NULL, 24);
	server->seat = wlr_seat_create(server->display, "seat0");
	if (server->cursor == NULL || server->cursor_manager == NULL ||
			server->seat == NULL) {
		wlr_log(WLR_ERROR, "%s", "could not initialize input devices");
		return false;
	}
	wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

	server->cursor_motion.notify = cursor_handle_motion;
	wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
	server->cursor_motion_absolute.notify = cursor_handle_motion_absolute;
	wl_signal_add(&server->cursor->events.motion_absolute,
		&server->cursor_motion_absolute);
	server->cursor_button.notify = cursor_handle_button;
	wl_signal_add(&server->cursor->events.button, &server->cursor_button);
	server->cursor_axis.notify = cursor_handle_axis;
	wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
	server->cursor_frame.notify = cursor_handle_frame;
	wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);
	server->request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server->seat->events.request_set_cursor,
		&server->request_cursor);
	server->request_selection.notify = seat_request_selection;
	wl_signal_add(&server->seat->events.request_set_selection,
		&server->request_selection);

	if (wl_event_loop_add_signal(loop, SIGINT, server_handle_signal, server) == NULL ||
			wl_event_loop_add_signal(loop, SIGTERM, server_handle_signal, server) == NULL ||
			wl_event_loop_add_signal(loop, SIGCHLD, server_reap_children, server) == NULL) {
		wlr_log(WLR_ERROR, "%s", "could not register shutdown signal handlers");
		return false;
	}
	server->ipc_fd = -1;
	return true;
}

int main(void) {
	wlr_log_init(WLR_INFO, NULL);
	struct nixde_server server = {0};
	if (!initialize_server(&server)) {
		return 1;
	}
	if (!server_create_ipc(&server)) {
		return 1;
	}

	const char *socket = wl_display_add_socket_auto(server.display);
	if (socket == NULL) {
		wlr_log(WLR_ERROR, "%s", "could not create Wayland socket");
		return 1;
	}
	if (!wlr_backend_start(server.backend)) {
		wlr_log(WLR_ERROR, "%s", "could not start wlroots backend");
		return 1;
	}
	setenv("WAYLAND_DISPLAY", socket, 1);
	setenv("XDG_SESSION_TYPE", "wayland", 1);
	setenv("XDG_SESSION_DESKTOP", "NixDE", 1);
	setenv("XDG_CURRENT_DESKTOP", "NixDE", 1);
	setenv("NIXDE_CONTROL_SOCKET", server.ipc_path, 1);

	char *const panel_argv[] = {"nixde-panel", NULL};
	int spawn_error = posix_spawnp(&server.panel_pid, panel_argv[0], NULL,
		NULL, panel_argv, environ);
	if (spawn_error != 0) {
		server.panel_pid = 0;
		wlr_log(WLR_ERROR, "could not start nixde-panel: %s",
			strerror(spawn_error));
	}

	wlr_log(WLR_INFO, "NixDE is running on WAYLAND_DISPLAY=%s", socket);
	wl_display_run(server.display);
	if (server.panel_pid > 0) {
		kill(server.panel_pid, SIGTERM);
		waitpid(server.panel_pid, NULL, 0);
	}
	wl_display_destroy_clients(server.display);
	wl_event_source_remove(server.ipc_source);
	close(server.ipc_fd);
	unlink(server.ipc_path);
	wl_list_remove(&server.new_output.link);
	wl_list_remove(&server.new_input.link);
	wl_list_remove(&server.new_toplevel.link);
	wl_list_remove(&server.new_popup.link);
	wl_list_remove(&server.output_layout_change.link);
	wl_list_remove(&server.request_cursor.link);
	wl_list_remove(&server.request_selection.link);
	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_xcursor_manager_destroy(server.cursor_manager);
	wl_list_remove(&server.cursor_motion.link);
	wl_list_remove(&server.cursor_motion_absolute.link);
	wl_list_remove(&server.cursor_button.link);
	wl_list_remove(&server.cursor_axis.link);
	wl_list_remove(&server.cursor_frame.link);
	wlr_cursor_destroy(server.cursor);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.display);
	return 0;
}
