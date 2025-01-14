#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <linux/input.h>

#include "config.h"

/* https://www.kernel.org/doc/html/latest/input/event-codes.html */
#define INPUT_VAL_PRESS 1
#define INPUT_VAL_RELEASE 0
#define INPUT_VAL_REPEAT 2


/**
 * Get an event from stdin
 */
int read_event(struct input_event *event) {
	return fread(event, sizeof(struct input_event), 1, stdin) == 1;
}


/**
 * Write an event to stdout
 */
void write_event(const struct input_event *event) {
	if (fwrite(event, sizeof(struct input_event), 1, stdout) != 1)
		exit(EXIT_FAILURE);
}


/**
 * @return the modifier mask of the given key
 */
unsigned int get_mod_mask(key_code key) {
	int i = 0, j;
	unsigned int bits;
	for (bits = 1 << (LENGTH(mods) - 1); bits; bits >>= 1) {
		for (j = 0; j < LENGTH(mods[i]); j++) {
			if (mods[i][j] == key) return bits;
		}
		i++;
	}
	return 0;
}


/**
 * Attempt to run associated command
 *
 * @return whether the key combination is a hotkey
 */
int try_hotkey(key_code key, unsigned int mod_state, pid_t pid, union sigval *msg) {
	int i;
	for (i = 0; i < LENGTH(bindings); i++) {
		if (bindings[i].key  == key
		 && bindings[i].mod_mask == mod_state) {
			msg->sival_int = i;
			sigqueue(pid, SIGUSR1, *msg);
			return 1;
		}
	}
	return 0;
}


int main(int argc, char *argv[]) {
	/* find hkd pid */
	FILE *cache = popen("pidof hkd", "r");
	char pid_str[20];
	fgets(pid_str, 20, cache);
	pid_t pid = strtoul(pid_str, NULL, 10);
	fclose(cache);
	if (pid == 0) {
		fprintf(stderr, "Error: hkd not running");
		exit(EXIT_FAILURE);
	}

	/* process events */
	struct input_event input;
	union sigval msg;
	key_code last_press = 0;
	unsigned int mod_state = 0;
	unsigned int mod_mask;
	setbuf(stdin, NULL), setbuf(stdout, NULL);
	while (read_event(&input)) {
		/* make mouse and touchpad events consume pressed taps */
		if (input.type == EV_MSC && input.code == MSC_SCAN) continue;

		/* forward anything that is not a key event, including SYNs */
		if (input.type != EV_KEY) {
			write_event(&input);
			continue;
		}

		/* process key */
		switch (input.value) {
			case INPUT_VAL_PRESS:
				last_press = input.code;
				if ((mod_mask = get_mod_mask(input.code))
				 || !try_hotkey(input.code, mod_state, pid, &msg)) {
					mod_state |= mod_mask;
					write_event(&input);
				}
				continue;
			case INPUT_VAL_RELEASE:
				if ((mod_mask = get_mod_mask(input.code))) {
					mod_state ^= mod_mask;
					if (last_press == input.code) try_hotkey(input.code, mod_state, pid, &msg);
				}
				write_event(&input);
				continue;
			case INPUT_VAL_REPEAT:
				/* linux console, X, wayland handles repeat */
				continue;
			default:
				fprintf(stderr, "unexpected .value=%d .code=%d, doing nothing",
				        input.value,
				        input.code);
				continue;
		}
	}

	return 0;
}
