#define JACE_VERSION "0.0.1"

#ifdef __linux__
#define _POSIX_C_SOURCE 200809L
#endif

#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <signal.h>

typedef struct erow {
	int idx;
	int size;
	int rsize;
	char *chars;
	char *render;
} erow;

struct editorConfig {
	int cx,cy;
	int rowoff;
	int coloff;
	int screenrows;
	int screencols;
	int numrows;
	int rawmode;
	erow *row;
	int dirty;
	char *filename;
	char statusmsg[80];
	time_t statusmsg_time;
};

static struct editorConfig E;

enum KEY_ACTION {
		KEY_NULL = 0,
		CTRL_C = 3,
		CTRL_D = 4,
		CTRL_F = 6,
		CTRL_H = 8,
		TAB = 9,
		CTRL_L = 12,
		ENTER = 13,
		CTRL_Q = 17,
		CTRL_S = 19,
		CTRL_U = 21,
		ESC = 27,
		BACKSPACE =  127,
		/* soft codes */
		ARROW_LEFT = 1000,
		ARROW_RIGHT,
		ARROW_UP,
		ARROW_DOWN,
		DEL_KEY,
		HOME_KEY,
		END_KEY,
		PAGE_UP,
		PAGE_DOWN
};

void editorSetStatusMessage(const char *fmt, ...);

/* ======================= low-level terminal handling ====================== */

static struct termios orig_termios;

void disableRawMode(int fd)
{
	if (E.rawmode) {
		tcsetattr(fd,TCSAFLUSH,&orig_termios);
		E.rawmode = 0;
	}
}

void editorAtExit(void)
{
	disableRawMode(STDIN_FILENO);
}

int enableRawMode(int fd)
{
	struct termios raw;

	if (E.rawmode) return 0;
	if (!isatty(STDIN_FILENO)) goto fatal;
	atexit(editorAtExit);
	if (tcgetattr(fd,&orig_termios) == -1) goto fatal;

	raw = orig_termios;

	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(fd,TCSAFLUSH,&raw) < 0) goto fatal;
	E.rawmode = 1;
	return 0;

fatal:
	errno = ENOTTY;
	return -1;
}

int editorReadKey(int fd)
{
	int nread;
	char c, seq[3];
	while ((nread = read(fd,&c,1)) == 0);
	if (nread == -1) exit(1);

	while(1) {
		switch(c) {
			case ESC:
			// if this is just an ESC, we'll timeout here
			if (read(fd,seq,1) == 0) return ESC;
			if (read(fd,seq+1,1) == 0) return ESC;

			// ESC [ sequences
			if (seq[0] == '[') {
				if (seq[1] >= '0' && seq[1] <= '9') {
					// extended escape, read additional byte
					if (read(fd,seq+2,1) == 0) return ESC;
					if (seq[2] == '~') {
						switch(seq[1]) {
						case '3': return DEL_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						}
					}
				} else {
					switch(seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
					}
				}
			}

			/* ESC O sequences. */
			else if (seq[0] == 'O') {
				switch(seq[1]) {
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
				}
			}
			break;
		default:
			return c;
		}
	}
}

int getCursorPosition(int ifd, int ofd, int *rows, int *cols)
{
	char buf[32];
	unsigned int i = 0;

	if (write(ofd, "\x1b[6n", 4) != 4) return -1;

	while (i < sizeof(buf)-1) {
		if (read(ifd,buf+i,1) != 1) break;
		if (buf[i] == 'R') break;
		i++;
	}
	buf[i] = '\0';

	if (buf[0] != ESC || buf[1] != '[') return -1;
	if (sscanf(buf+2,"%d;%d",rows,cols) != 2) return -1;
	return 0;
}

int getWindowSize(int ifd, int ofd, int *rows, int *cols)
{
	struct winsize ws;

	if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		int orig_row, orig_col, retval;

		retval = getCursorPosition(ifd,ofd,&orig_row,&orig_col);
		if (retval == -1) goto failed;

		if (write(ofd,"\x1b[999C\x1b[999B",12) != 12) goto failed;
		retval = getCursorPosition(ifd,ofd,rows,cols);
		if (retval == -1) goto failed;

		char seq[32];
		snprintf(seq,32,"\x1b[%d;%dH",orig_row,orig_col);
		if (write(ofd,seq,strlen(seq)) == -1) {
			// Can't recover
		}
		return 0;
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}

failed:
	return -1;
}

/* ======================= editor rows implementation ======================= */

void editorUpdateRow(erow *row)
{
	unsigned int tabs = 0, nonprint = 0;
	int j, idx;

	free(row->render);
	for (j = 0; j < row->size; j++)
		if (row->chars[j] == TAB) tabs++;

	unsigned long long allocsize =
		(unsigned long long) row->size + tabs*8 + nonprint*9 + 1;
	if (allocsize > UINT32_MAX) {
		printf("Some lines of the edited file is too long for JACE.\n");
		exit(1);
	}

	row->render = malloc(row->size + tabs*8 + nonprint*9 + 1);
	idx = 0;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == TAB) {
			row->render[idx++] = ' ';
			while(idx % 8 != 0) row->render[idx++] = ' ';
		} else {
			row->render[idx++] = row->chars[j];
		}
	}
	row->rsize = idx;
	row->render[idx] = '\0';
}

void editorInsertRow(int at, char *s, size_t len)
{
	if (at > E.numrows) return;
	E.row = realloc(E.row,sizeof(erow)*(E.numrows+1));
	
	if (at != E.numrows) {
		memmove(E.row+at+1,E.row+at,sizeof(E.row[0])*(E.numrows-at));
		for (int j = at+1; j <= E.numrows; j++) E.row[j].idx++;
	}
	
	E.row[at].size = len;
	E.row[at].chars = malloc(len+1);
	memcpy(E.row[at].chars,s,len+1);
	E.row[at].render = NULL;
	E.row[at].rsize = 0;
	E.row[at].idx = at;
	editorUpdateRow(E.row+at);
	E.numrows++;
	E.dirty++;
}

void editorFreeRow(erow *row)
{
	free(row->render);
	free(row->chars);
}

void editorDelRow(int at)
{
	erow *row;

	if (at >= E.numrows) return;
	row = E.row+at;
	editorFreeRow(row);
	memmove(E.row+at,E.row+at+1,sizeof(E.row[0])*(E.numrows-at-1));
	
	for (int j = at; j < E.numrows-1; j++) E.row[j].idx++;
	E.numrows--;
	E.dirty++;
}

char *editorRowsToString(int *buflen)
{
	char *buf = NULL, *p;
	int totlen = 0;
	int j;

	for (j = 0; j < E.numrows; j++)
		totlen += E.row[j].size+1;
	*buflen = totlen;
	totlen++;

	p = buf = malloc(totlen);
	for (j = 0; j < E.numrows; j++) {
		memcpy(p,E.row[j].chars,E.row[j].size);
		p += E.row[j].size;
		*p = '\n';
		p++;
	}
	*p = '\0';
	return buf;
}

void editorRowInsertChar(erow *row, int at, int c)
{
	if (at > row->size) {
		int padlen = at-row->size;
		row->chars = realloc(row->chars,row->size+padlen+2);
		memset(row->chars+row->size,' ',padlen);
		row->chars[row->size+padlen+1] = '\0';
		row->size += padlen+1;
	} else {
		row->chars = realloc(row->chars,row->size+2);
		memmove(row->chars+at+1,row->chars+at,row->size-at+1);
		row->size++;
	}
	row->chars[at] = c;
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len)
{
	row->chars = realloc(row->chars,row->size+len+1);
	memcpy(row->chars+row->size,s,len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowDelChar(erow *row, int at)
{
	if (row->size <= at) return;
	memmove(row->chars+at,row->chars+at+1,row->size-at);
	editorUpdateRow(row);
	row->size--;
	E.dirty++;
}

void editorInsertChar(int c)
{
	int filerow = E.rowoff+E.cy;
	int filecol = E.coloff+E.cx;
	erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

	if (!row) {
		while(E.numrows <= filerow)
			editorInsertRow(E.numrows,"",0);
	}
	row = &E.row[filerow];
	editorRowInsertChar(row,filecol,c);
	if (E.cx == E.screencols-1)
		E.coloff++;
	else
		E.cx++;
	E.dirty++;
}

void editorInsertNewline(void)
{
	int filerow = E.rowoff+E.cy;
	int filecol = E.coloff+E.cx;
	erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

	if (!row) {
		if (filerow == E.numrows) {
			editorInsertRow(filerow,"",0);
			goto fixcursor;
		}
		return;
	}
	
	if (filecol >= row->size) filecol = row->size;
	if (filecol == 0) {
		editorInsertRow(filerow,"",0);
	} else {
		editorInsertRow(filerow+1,row->chars+filecol,row->size-filecol);
		row = &E.row[filerow];
		row->chars[filecol] = '\0';
		row->size = filecol;
		editorUpdateRow(row);
	}
fixcursor:
	if (E.cy == E.screenrows-1) {
		E.rowoff++;
	} else {
		E.cy++;
	}
	E.cx = 0;
	E.coloff = 0;
}

void editorDelChar(int back)
{
	int filerow = E.rowoff+E.cy;
	int filecol = E.coloff+E.cx;

	if (filerow >= E.numrows) return;

	erow *row = &E.row[filerow];

	if (back) {
		if (filecol == 0 && filerow == 0) return;
		if (back && filecol == 0) {
			filecol = E.row[filerow-1].size;
			editorRowAppendString(&E.row[filerow-1],row->chars,row->size);
			editorDelRow(filerow);
			row = NULL;
			if (E.cy == 0)
				E.rowoff--;
			else
				E.cy--;
			E.cx = filecol;
			if (E.cx >= E.screencols) {
				int shift = (E.screencols-E.cx)+1;
				E.cx -= shift;
				E.coloff += shift;
			}
		} else {
			editorRowDelChar(row,filecol-1);
			if (E.cx == 0 && E.coloff)
				E.coloff--;
			else
				E.cx--;
		}
	} else {
		if (filerow == E.numrows-1 && filecol >= row->size) return;

		if (filecol == row->size) {
			erow *nextrow = (filerow + 1 >= E.numrows) ? NULL : &E.row[filerow + 1];
			editorRowAppendString(&E.row[filerow],nextrow->chars,nextrow->size);
			editorDelRow(filerow + 1);
			row = NULL;
		} else {
			editorRowDelChar(row,filecol);
		}
	}

	if (row) editorUpdateRow(row);
	E.dirty++;
}

int editorOpen(char *filename)
{
	FILE *fp;

	E.dirty = 0;
	free(E.filename);
	size_t fnlen = strlen(filename)+1;
	E.filename = malloc(fnlen);
	memcpy(E.filename,filename,fnlen);

	fp = fopen(filename,"r");
	if (!fp) {
		if (errno != ENOENT) {
			perror("Opening file");
			exit(1);
		}
		return 1;
	}

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;

	while((linelen = getline(&line,&linecap,fp)) != -1) {
		if (linelen && (line[linelen-1] == '\n' || line[linelen-1] == '\r'))
			line[--linelen] = '\0';
		editorInsertRow(E.numrows,line,linelen);
	}

	free(line);
	fclose(fp);
	E.dirty = 0;
	return 0;
}

// save the current file on disk
// returns 0 on success, 1 on error
int editorSave(void)
{
	int len;
	char *buf = editorRowsToString(&len);
	int fd = open(E.filename,O_RDWR|O_CREAT,0644);
	if (fd == -1) goto writeerr;

	if (ftruncate(fd,len) == -1) goto writeerr;
	if (write(fd,buf,len) != len) goto writeerr;

	close(fd);
	free(buf);
	E.dirty = 0;
	editorSetStatusMessage("%d bytes written on disk", len);
	return 0;

writeerr:
	free(buf);
	if (fd != -1) close(fd);
	editorSetStatusMessage("Can't save! I/O error: %s",strerror(errno));
	return 1;
}

/* ============================= terminal update ============================ */

// append buffer to avoid flickering effects
struct abuf {
	char *b;
	int len;
};

#define ABUF_INIT {NULL,0}

void abAppend(struct abuf *ab, const char *s, int len)
{
	char *new = realloc(ab->b,ab->len+len);

	if (new == NULL) return;
	memcpy(new+ab->len,s,len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf *ab)
{
	free(ab->b);
}

static void hideCursor(struct abuf *ab)
{
	abAppend(ab, "\x1b[?25l",6); // hide cursor
	abAppend(ab, "\x1b[H",3);    // go home
}

static void setCursor(struct abuf *ab)
{
	int j;
	int cx = 1;
	int filerow = E.rowoff+E.cy;
	char buf[32];

	erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
	if (row) {
		for (j = E.coloff; j < (E.cx+E.coloff); j++) {
			if (j < row->size && row->chars[j] == TAB) cx += 8 - ((cx) % 8);
			cx++;
		}
	}

	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy+1, cx);
	abAppend(ab, buf, strlen(buf));
	abAppend(ab, "\x1b[?25h", 6); // show cursor
}

static void setStatus(struct abuf *ab, int putcursor)
{
	if (putcursor) {
		char buf[32];
		snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.screenrows+1,0);
		abAppend(ab, buf, strlen(buf));
	}

	// first row
	abAppend(ab, "\x1b[0K", 4);
	abAppend(ab, "\x1b[7m", 4);

	char status[80];
	char rstatus[80];
	int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", E.filename, E.numrows, E.dirty ? "(modified)" : "");
	int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.rowoff + E.cy + 1, E.numrows);
	
	if (len > E.screencols) len = E.screencols;
	abAppend(ab, status, len);
	while (len < E.screencols) {
		if (E.screencols - len == rlen) {
			abAppend(ab, rstatus, rlen);
			break;
		} else {
			abAppend(ab, " ", 1);
			len++;
		}
	}
	abAppend(ab, "\x1b[0m\r\n", 6);

	// second row
	abAppend(ab, "\x1b[0K", 4);
	int msglen = strlen(E.statusmsg);
	if (msglen && time(NULL) - E.statusmsg_time < 5)
		abAppend(ab, E.statusmsg, msglen <= E.screencols ? msglen : E.screencols);
}

void editorRefreshScreen(void)
{
	int y;
	erow *r;
	struct abuf ab = ABUF_INIT;

	hideCursor(&ab);

	for (y = 0; y < E.screenrows; y++) {
		int filerow = E.rowoff+y;

		if (filerow >= E.numrows) {
			if (E.numrows == 0 && y == E.screenrows/3) {
				char welcome[80];
				int welcomelen = snprintf(welcome,sizeof(welcome),
					"Joe's Awesome Code Editor -- verison %s\x1b[0K\r\n", JACE_VERSION);
				int padding = (E.screencols-welcomelen)/2;
				if (padding) {
					abAppend(&ab,"~",1);
					padding--;
				}
				while(padding--) abAppend(&ab," ",1);
				abAppend(&ab,welcome,welcomelen);
			} else {
				abAppend(&ab,"~\x1b[0K\r\n",7);
			}
			continue;
		}

		r = &E.row[filerow];

		int len = r->rsize - E.coloff;
		if (len > 0) {
			if (len > E.screencols) len = E.screencols;
			char *c = r->render+E.coloff;
			int j;
			for (j = 0; j < len; j++) {
				abAppend(&ab,c+j,1);
			}
		}

		abAppend(&ab,"\x1b[39m",5);
		abAppend(&ab,"\x1b[0K",4);
		abAppend(&ab,"\r\n",2);
	}

	setStatus(&ab, 0);
	setCursor(&ab);
	write(STDOUT_FILENO,ab.b,ab.len);
	abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...)
{
	va_list ap;
	va_start(ap,fmt);
	vsnprintf(E.statusmsg,sizeof(E.statusmsg),fmt,ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

/* =============================== find mode ================================ */

#define JACE_QUERY_LEN 256

void editorFind(int fd)
{
	char query[JACE_QUERY_LEN+1] = {0};
	int qlen = 0;
	int last_match = -1; // last line where a match was found. -1 for none
	int find_next = 0; // if 1 search next, if -1 search prev

	int saved_cx = E.cx, saved_cy = E.cy;
	int saved_coloff = E.coloff, saved_rowoff = E.rowoff;

	while(1) {
		editorSetStatusMessage(
			"Search: %s (Use ESC/Arrows/Enter)", query);
		editorRefreshScreen();

		int c = editorReadKey(fd);
		if (c == DEL_KEY || c == CTRL_H || c == BACKSPACE) {
			if (qlen != 0) query[--qlen] = '\0';
			last_match = -1;
		} else if (c == ESC || c == ENTER) {
			if (c == ESC) {
				E.cx = saved_cx; E.cy = saved_cy;
				E.coloff = saved_coloff; E.rowoff = saved_rowoff;
			}
			editorSetStatusMessage("");
			return;
		} else if (c == ARROW_RIGHT || c == ARROW_DOWN) {
			find_next = 1;
		} else if (c == ARROW_LEFT || c == ARROW_UP) {
			find_next = -1;
		} else if (isprint(c)) {
			if (qlen < JACE_QUERY_LEN) {
				query[qlen++] = c;
				query[qlen] = '\0';
				last_match = -1;
			}
		}

		// search occurrence
		if (last_match == -1) find_next = 1;
		if (find_next) {
			char *match = NULL;
			int match_offset = 0;
			int i, current = last_match;

			for (i = 0; i < E.numrows; i++) {
				current += find_next;
				if (current == -1) current = E.numrows-1;
				else if (current == E.numrows) current = 0;
				match = strstr(E.row[current].render,query);
				if (match) {
					match_offset = match-E.row[current].render;
					break;
				}
			}
			find_next = 0;

			if (match) {
				last_match = current;

				E.cy = 0;
				E.cx = match_offset;
				E.rowoff = current;
				E.coloff = 0;
				// scroll horizontally as needed
				if (E.cx > E.screencols) {
					int diff = E.cx - E.screencols;
					E.cx -= diff;
					E.coloff += diff;
				}
			}
		}
	}
}

/* ========================= editor events handling  ======================== */

// cursor position handler
void editorMoveCursor(int key)
{
	int filerow = E.rowoff+E.cy;
	int filecol = E.coloff+E.cx;
	int rowlen;
	erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

	switch(key) {
	case ARROW_LEFT:
		if (E.cx == 0) {
			if (E.coloff) {
				E.coloff--;
			} else {
				if (filerow > 0) {
					E.cy--;
					E.cx = E.row[filerow-1].size;
					if (E.cx > E.screencols-1) {
						E.coloff = E.cx-E.screencols+1;
						E.cx = E.screencols-1;
					}
				}
			}
		} else {
			E.cx -= 1;
		}
		break;
	case ARROW_RIGHT:
		if (row && filecol < row->size) {
			if (E.cx == E.screencols-1) {
				E.coloff++;
			} else {
				E.cx += 1;
			}
		} else if (row && filecol == row->size) {
			E.cx = 0;
			E.coloff = 0;
			if (E.cy == E.screenrows-1) {
				E.rowoff++;
			} else {
				E.cy += 1;
			}
		}
		break;
	case ARROW_UP:
		if (E.cy == 0) {
			if (E.rowoff) E.rowoff--;
		} else {
			E.cy -= 1;
		}
		break;
	case ARROW_DOWN:
		if (filerow < E.numrows) {
			if (E.cy == E.screenrows-1) {
				E.rowoff++;
			} else {
				E.cy += 1;
			}
		}
		break;
	case HOME_KEY:
		E.cx = 0;
		E.coloff = 0;
		break;
	case END_KEY:
		E.cx = E.row[filerow].size;
		if (E.cx > E.screencols - 1) {
			E.coloff = E.cx - E.screencols + 1;
			E.cx = E.screencols - 1;
		}
		break;
	}

	// fix cx if the current line doesn't have enough chars
	filerow = E.rowoff+E.cy;
	filecol = E.coloff+E.cx;
	row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
	rowlen = row ? row->size : 0;
	if (filecol > rowlen) {
		E.cx -= filecol-rowlen;
		if (E.cx < 0) {
			E.coloff += E.cx;
			E.cx = 0;
		}
	}
}

int editorProcessKeypress(int fd)
{
	static int quit_times = 1;
	int ret = 1;

	int c = editorReadKey(fd);
	switch(c) {
	case ENTER:
		editorInsertNewline();
		break;
	case CTRL_C:
		break;
	case CTRL_Q:
		// quit if the file was already saved.
		if (E.dirty && quit_times) {
			editorSetStatusMessage("WARNING!!! File has unsaved changes. "
				"Press Ctrl-Q again to quit.", quit_times);
			quit_times--;
			return ret;
		} else {
			char buf[32];
			struct abuf ab = ABUF_INIT;
			snprintf(buf, sizeof(buf), "\x1b[%d;%dH\r\n",E.screenrows+2,1);
			abAppend(&ab,buf,strlen(buf));
			write(STDOUT_FILENO,ab.b,ab.len);
			abFree(&ab);
			exit(0);
		}
		break;
	case CTRL_S:
		editorSave();
		break;
	case CTRL_F:
		editorFind(fd);
		break;
	case BACKSPACE:
	case CTRL_H:
		editorDelChar(1);
		break;
	case DEL_KEY:
		editorDelChar(0);
		break;
	case PAGE_UP:
	case PAGE_DOWN:
		if (c == PAGE_UP && E.cy != 0)
			E.cy = 0;
		else if (c == PAGE_DOWN && E.cy != E.screenrows-1)
			E.cy = E.screenrows-1;

		{
		int times = E.screenrows;
		while(times--)
			editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
		}
		break;
	case HOME_KEY:
	case END_KEY:
	case ARROW_UP:
	case ARROW_DOWN:
	case ARROW_LEFT:
	case ARROW_RIGHT:
		ret = E.rowoff;
		editorMoveCursor(c);
		ret = (ret != E.rowoff);
		if (!ret) ret = (E.coloff > E.screencols);
		break;
	case CTRL_L:
		break;
	case ESC:
		break;
	default:
		editorInsertChar(c);
		break;
	}

	quit_times = 1; // reset it to the original value
	return ret;
}

int editorFileWasModified(void)
{
	return E.dirty;
}

void updateWindowSize(void)
{
	if (getWindowSize(STDIN_FILENO,STDOUT_FILENO, &E.screenrows,&E.screencols) == -1) {
		perror("Unable to query the screen for size (columns / rows)");
		exit(1);
	}
	
	E.screenrows -= 2; // get room for status bar
}

void handleSigWinCh(int unused __attribute__((unused)))
{
	updateWindowSize();
	if (E.cy > E.screenrows) E.cy = E.screenrows - 1;
	if (E.cx > E.screencols) E.cx = E.screencols - 1;
	editorRefreshScreen();
}

void initEditor(void)
{
	E.cx = 0;
	E.cy = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = NULL;
	E.dirty = 0;
	E.filename = NULL;
	updateWindowSize();
	signal(SIGWINCH, handleSigWinCh);
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr,"Usage: jace <filename>\n");
		exit(1);
	}

	initEditor();
	editorOpen(argv[1]);
	enableRawMode(STDIN_FILENO);
	editorSetStatusMessage("HELP: Ctrl-S = Save | Ctrl-Q = Quit | Ctrl-F = Find");
	editorRefreshScreen();

	while(1) {
		if (!editorProcessKeypress(STDIN_FILENO)) {
			struct abuf ab = ABUF_INIT;
			hideCursor(&ab);
			setStatus(&ab, 1);
			setCursor(&ab);
			write(STDOUT_FILENO, ab.b, ab.len);
			abFree(&ab);
		} else {
			editorRefreshScreen();
		}
	}

	return 0;
}
