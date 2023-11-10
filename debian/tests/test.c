#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <histedit.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

/* This function works exactly like fmemopen(), except that the memory stream
 * has an underlying file descriptor and hence can be used with libedit.
 * The mode argument is included for symmetry, but we really don't need it;
 * we always call it specified as "r" in this simple demo program.
 * As always, we return NULL with errno set on failure. */
static FILE *fmemopen_with_fd(const void *restrict bytes, size_t len, const char mode[restrict static 1]) {
	if(mode[0] != 'r' || mode[1]) {
		errno = EINVAL;
		return NULL;
	}

	int pip[2];
	if(pipe(pip) == -1) {
		return NULL;
	}

	/* We are not going to read from the pipe until we've completely written to it.
	 * Because of this, we avoid a deadlock if the pipe buffer gets full. */
	if(fcntl(pip[1], F_SETFD, O_NONBLOCK) == -1) {
closebothends:
		const int esave = errno;
		close(pip[1]);
		close(pip[0]);
		errno = esave;
		return NULL;
	}

	ssize_t k;
	size_t bytes_written = 0;
	do {
		k = write(pip[1], bytes_written + (char*)bytes, len - bytes_written);
		if(k == -1) {
			break;
		} else {
			bytes_written += k;
		}
	} while(bytes_written < len);
	if(k == -1) {
		if(errno == EAGAIN) {
			errno = ERANGE;
		}
		goto closebothends;
	}

	if(close(pip[1]) == -1) {
		const int esave = errno;
		close(pip[0]);
		errno = esave;
		return NULL;
	}

	FILE *const stream = fdopen(pip[0], "r");
	if(!stream) {
		const int esave = errno;
		close(pip[0]);
		errno = esave;
		return NULL;
	}
	return stream;
}

int main(void) {
	if(!setlocale(LC_ALL, "")) {
		fputs("Failed to enable default locale\n", stderr);
		exit(EXIT_FAILURE);
	}

	const char *const mbstest = "Test string";
	wchar_t *const wcstest = L"Test string";
	FILE *const instream = fmemopen_with_fd(mbstest, strlen(mbstest), "r");
	if(!instream) {
		perror("Failed to create stream");
		exit(EXIT_FAILURE);
	}
	EditLine *const el = el_init("test", instream, stdout, stderr);
	if(!el) {
		fputs("Failed to initialize libedit\n", stderr);
		if(fclose(instream) == EOF) {
			perror("Failed to close stream");
		}
	}

	const char *line = el_gets(el, &(int){0});
	if(!line) {
		if(feof(instream)) {
			fputs("End of file\n", stderr);
		} else {
			perror("Failed to read line");
		}
		el_end(el);
		if(fclose(instream) == EOF) {
			perror("Failed to close stream");
		}
		exit(EXIT_FAILURE);
	}
	if(fclose(instream) == EOF) {
		perror("Failed to close stream");
		el_end(el);
		exit(EXIT_FAILURE);
	}

	if(strcmp(mbstest, line)) {
		fputs("Strings did not match\n", stderr);
		abort();
	}
	el_end(el);

	/* Now let's try a wide-oriented stream.
	 * Note: We assume here that the external representation for a wide-oriented stream
	 * is the ordinary multibyte encoding. This may not be a good assumption for some locales. */
	FILE *const widestream = fmemopen_with_fd(mbstest, strlen(mbstest), "r");
	if(!widestream) {
		perror("Failed to create stream");
		exit(EXIT_FAILURE);
	}

	errno = 0;
	if(fwide(widestream, 1) <= 0) {
		if(errno) {
			perror("Failed to set wide orientation of stream");
		} else {
			fputs("Failed to set wide orientation of stream\n", stderr);
		}
		if(fclose(widestream) == EOF) {
			perror("Failed to close stream");
		}
		exit(EXIT_FAILURE);
	}

	EditLine *const elw = el_init("test", widestream, stdout, stderr);
	if(!elw) {
		fputs("Failed to initialize libedit\n", stderr);
		if(fclose(widestream) == EOF) {
			perror("Failed to close stream");
		}
		exit(EXIT_FAILURE);
	}

	const wchar_t *const wline = el_wgets(elw, &(int){0});
	if(!wline) {
		if(feof(widestream)) {
			fputs("Reached end of file\n", stderr);
		} else {
			puts("Failed to read line");
		}
		el_end(elw);
		if(fclose(widestream) == EOF) {
			perror("Failed to close stream");
		}
	}

	if(wcscmp(wline, wcstest)) {
		fputs("String comparison failed\n", stderr);
		abort();
	}

	el_end(elw);
}
