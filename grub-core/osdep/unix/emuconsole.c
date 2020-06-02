/*  console.c -- console for GRUB.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <config-util.h>

#include <grub/term.h>
#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/time.h>
#include <grub/terminfo.h>
#include <grub/dl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <langinfo.h>

#include <grub/emu/console.h>

#include <stdio.h>
#include <errno.h>

extern struct grub_terminfo_output_state grub_console_terminfo_output;
static int original_fl;
static int saved_orig;
static struct termios orig_tty;
static struct termios new_tty;
static int console_mode = 0;

#define MAX_LEN 1023

static int
dummy (void)
{
  return 0;
}
#if 0
static char msg[MAX_LEN+1];
static  void
dprint (int len)
{
  if (len < 0)
    return;
  if (len > MAX_LEN)
    len = MAX_LEN;
  write (2, msg, len);
}
#define dprintf(fmt, vargs...) dprint(snprintf(msg, MAX_LEN, fmt, ## vargs))
#else
#define dprintf(fmt, vargs...) {}
#endif

static void
put (struct grub_term_output *term, const int c)
{
  char chr = c;
  ssize_t actual;
  struct grub_terminfo_output_state *data
    = (struct grub_terminfo_output_state *) term->data;

  if (term->flags & GRUB_TERM_DUMB) {
    if (c == '\n') {
      data->pos.y++;
      data->pos.x = 0;
    } else {
      data->pos.x++;
    }
    if (0) {
      if (c == ' ') chr = '_';
      if (c == GRUB_TERM_BACKSPACE) chr = '{';
      if (c == '\b') chr = '<';
    }
  }

  actual = write (STDOUT_FILENO, &chr, 1);
  if (actual < 1)
    {
      /* We cannot do anything about this, but some systems require us to at
	 least pretend to check the result.  */
    }
}

static int
readkey (struct grub_term_input *term)
{
  grub_uint8_t c;
  ssize_t actual;

  fd_set readfds;
  struct timeval timeout;
  int sel;
  FD_SET (0, &readfds);
  timeout.tv_sec = 0;
  timeout.tv_usec = 500000;
  if ((sel=select (1, &readfds, (fd_set *)0, (fd_set *)0, &timeout)) <= 0)
    {
      if (sel < 0 && errno == EINTR)
        return 0x03; /* '^C' */
      return -1;
    }

  actual = read (STDIN_FILENO, &c, 1);
  if (actual > 0)
    return c;
  return -1;
}

#if defined(__s390x__)
#define NO_KEY	((grub_uint8_t)-1)
static int
readkey_dumb (struct grub_term_input *term)
{
  grub_uint8_t c;
  static grub_uint8_t p = NO_KEY;

  c = readkey (term);
  if (c == NO_KEY)
    return -1;
  if ((p == '^' || p == '\n') && c == '\n')   /* solitary '^' or '\n'? */
    {
      c = p;	/* use immediately! */
      p = '\n';
    }
  else if ((c == '\n' || c == '^') && p != c) /* non-duplicate specials? */
    {
      p = c;	/* remember! */
      c = NO_KEY;
    }
  else if (p == '^')
    {
      if (c != '^')
        c &= 0x1F;
      p = NO_KEY;
    }
  else
    p = c;
  return c;
}
#endif

static void
grub_dumb_putchar (struct grub_term_output *term,
                    const struct grub_unicode_glyph *c)
{
  unsigned i;

  /* For now, do not try to use a surrogate pair.  */
  if (c->base > 0xffff)
    put (term, '?');
  else
    put (term, (c->base & 0xffff));

 if (0) {
  for (i = 0; i < c->ncomb; i++)
    if (c->base < 0xffff)
      put (term, grub_unicode_get_comb (c)[i].code);
 }
}

static struct grub_term_coordinate
grub_dumb_getxy (struct grub_term_output *term)
{
  struct grub_terminfo_output_state *data
    = (struct grub_terminfo_output_state *) term->data;

  dprintf ("<%d,%d>", data->pos.x, data->pos.y);
  return data->pos;
}

static struct grub_term_coordinate
grub_dumb_getwh (struct grub_term_output *term)
{
  static int once = 0;
  struct grub_terminfo_output_state *data
    = (struct grub_terminfo_output_state *) term->data;

  if (!once++)
    dprintf ("dumb_getwh: w=%d h=%d\n", data->size.x, data->size.y);
  return data->size;
}

static void
grub_dumb_gotoxy (struct grub_term_output *term,
                      struct grub_term_coordinate pos)
{
  struct grub_terminfo_output_state *data
    = (struct grub_terminfo_output_state *) term->data;

  if (pos.x > grub_term_width (term) || pos.y > grub_term_height (term))
    {
      grub_error (GRUB_ERR_BUG, "invalid point (%u,%u)", pos.x, pos.y);
      return;
    }

  dprintf("goto(%d,%d)", pos.x, pos.y);
  if (pos.x > (grub_term_width (term) - 4)) {
    dprintf (" really?");
    //return;
  }

  if (data->gotoxy)
    {
      int i;
      dprintf ("data-gotoxy");
      if (data->pos.y != pos.y) {
        put (term, '\n');

        for (i = 1; i < pos.x; i++ )
         put (term, ' ');
      }
    }
  else
    {
      int i = 0;
      if (data->pos.y != pos.y || data->pos.x > pos.x) {
        if (data->pos.y >= pos.y) data->pos.y = pos.y - 1;
        if (pos.y - data->pos.y > 3) data->pos.y = pos.y - 2;
        dprintf (" <%dnl>+%d", (pos.y - data->pos.y), pos.x);
        for (i = data->pos.y; i < pos.y; i++ )
          put (term, '\n');
      }
      for (i = data->pos.x; i < pos.x; i++ )
          put (term, ' ');
      dprintf ("#%d", i);
      grub_dumb_getxy (term);
    }

  dprintf ("\n");
  data->pos = pos;
}

static grub_err_t
grub_console_init_input (struct grub_term_input *term)
{
  if (!saved_orig)
    {
      original_fl = fcntl (STDIN_FILENO, F_GETFL);
      fcntl (STDIN_FILENO, F_SETFL, original_fl | O_NONBLOCK);
    }

  saved_orig = 1;

  tcgetattr(STDIN_FILENO, &orig_tty);
  new_tty = orig_tty;
  new_tty.c_lflag &= ~(ICANON | ECHO);
  new_tty.c_cc[VMIN] = 1;
  tcsetattr(STDIN_FILENO, TCSANOW, &new_tty);

  return grub_terminfo_input_init (term);
}

static grub_err_t
grub_console_fini_input (struct grub_term_input *term
		       __attribute__ ((unused)))
{
  fcntl (STDIN_FILENO, F_SETFL, original_fl);
  tcsetattr(STDIN_FILENO, TCSANOW, &orig_tty);
  saved_orig = 0;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_console_init_output (struct grub_term_output *term)
{
  struct winsize size;
  if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &size) >= 0 &&
      size.ws_col > 0 && size.ws_row > 0)
    {
      grub_console_terminfo_output.size.x = size.ws_col;
      grub_console_terminfo_output.size.y = size.ws_row;
    }
  else
    {
      grub_console_terminfo_output.size.x = 80;
      grub_console_terminfo_output.size.y = 24;
    }
  if (console_mode == 3215)
    grub_console_terminfo_output.size.x -= 1;

  grub_terminfo_output_init (term);

  return 0;
}



struct grub_terminfo_input_state grub_console_terminfo_input =
  {
    .readkey = readkey
  };

struct grub_terminfo_output_state grub_console_terminfo_output =
  {
    .put = put,
    .size = { 80, 24 }
  };

static struct grub_term_input grub_console_term_input =
  {
    .name = "console",
    .init = grub_console_init_input,
    .fini = grub_console_fini_input,
    .getkey = grub_terminfo_getkey,
    .data = &grub_console_terminfo_input
  };

static struct grub_term_output grub_console_term_output =
  {
    .name = "console",
    .init = grub_console_init_output,
    .putchar = grub_terminfo_putchar,
    .getxy = grub_terminfo_getxy,
    .getwh = grub_terminfo_getwh,
    .gotoxy = grub_terminfo_gotoxy,
    .cls = grub_terminfo_cls,
    .setcolorstate = grub_terminfo_setcolorstate,
    .setcursor = grub_terminfo_setcursor,
    .data = &grub_console_terminfo_output,
    .progress_update_divisor = GRUB_PROGRESS_FAST
  };

void
grub_console_init (void)
{
#if ! defined(__s390x__)
  const char *cs = nl_langinfo (CODESET);
  if (cs && grub_strcasecmp (cs, "UTF-8"))
    grub_console_term_output.flags = GRUB_TERM_CODE_TYPE_UTF8_LOGICAL;
  else
    grub_console_term_output.flags = GRUB_TERM_CODE_TYPE_ASCII;
#else
  char link[MAX_LEN+1];
  ssize_t len = readlink ("/proc/self/fd/0", link, MAX_LEN);

  if (len > 0)
    link[len] = 0;
  else
    link[0] = 0;
  if (grub_strncmp ("/dev/ttyS", link, 9) == 0 )
    console_mode = 3215;
  else if (grub_strncmp ("/dev/3270/tty", link, 13) == 0 )
    console_mode = 3270;
  else if (grub_strncmp ("/dev/sclp_line", link, 14) == 0 )
    console_mode = 3215;
  grub_console_term_output.flags = GRUB_TERM_CODE_TYPE_ASCII;
  switch (console_mode)
    {
      case 3215:
       grub_console_term_output.flags |= GRUB_TERM_DUMB;
       /* FALLTHROUGH */
      case 3270:
       grub_console_term_output.flags |= GRUB_TERM_LINE;
       grub_console_term_output.flags |= GRUB_TERM_NO_ECHO;
       grub_console_terminfo_input.readkey = readkey_dumb;
       break;
      default:
       break;
    }
#endif
  if (grub_console_term_output.flags & GRUB_TERM_DUMB)
    {
      grub_console_term_output.putchar = grub_dumb_putchar,
      grub_console_term_output.getxy = grub_dumb_getxy;
      grub_console_term_output.getwh = grub_dumb_getwh;
      grub_console_term_output.gotoxy = grub_dumb_gotoxy;
      grub_console_term_output.cls = (void *) dummy;
      grub_console_term_output.setcolorstate = (void *) dummy;
      grub_console_term_output.setcursor = (void *) dummy;
      grub_console_term_output.progress_update_divisor = GRUB_PROGRESS_NO_UPDATE;
    }
  grub_term_register_input ("console", &grub_console_term_input);
  grub_term_register_output ("console", &grub_console_term_output);
  grub_terminfo_init ();
  grub_terminfo_output_register (&grub_console_term_output,
    (grub_console_term_output.flags & GRUB_TERM_DUMB) ? "dumb":"vt100-color");
}

void
grub_console_fini (void)
{
  dprintf( "grub_console_fini: %d\n", grub_console_term_output.flags & GRUB_TERM_DUMB);
  if (saved_orig)
    {
      fcntl (STDIN_FILENO, F_SETFL, original_fl);
      tcsetattr(STDIN_FILENO, TCSANOW, &orig_tty);
    }
  if (!(grub_console_term_output.flags & GRUB_TERM_DUMB))
    {
      const char clear[] = { 0x1b, 'c', 0 };
      write (STDOUT_FILENO, clear, 2);
    }
  saved_orig = 0;
}
