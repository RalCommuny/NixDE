#include "panel.h"

#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

gboolean panel_control_request(NixdePanel *panel, const char *request,
		char **response_out) {
	if (response_out != NULL) {
		*response_out = NULL;
	}
	if (panel->control_socket == NULL ||
			strlen(panel->control_socket) >=
				sizeof(((struct sockaddr_un *)0)->sun_path)) {
		return FALSE;
	}

	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		return FALSE;
	}
	struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

	struct sockaddr_un address = {.sun_family = AF_UNIX};
	g_strlcpy(address.sun_path, panel->control_socket,
		sizeof(address.sun_path));
	if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		close(fd);
		return FALSE;
	}

	size_t request_length = strlen(request);
	size_t sent = 0;
	while (sent < request_length) {
		ssize_t result = send(fd, request + sent, request_length - sent,
			MSG_NOSIGNAL);
		if (result < 0 && errno == EINTR) {
			continue;
		}
		if (result <= 0) {
			close(fd);
			return FALSE;
		}
		sent += (size_t)result;
	}

	if (response_out != NULL) {
		GString *response = g_string_new(NULL);
		char buffer[4096];
		for (;;) {
			ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
			if (received < 0 && errno == EINTR) {
				continue;
			}
			if (received <= 0) {
				break;
			}
			g_string_append_len(response, buffer, received);
		}
		*response_out = g_string_free(response, FALSE);
	}
	close(fd);
	return TRUE;
}
