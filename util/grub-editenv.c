/* grub-editenv.c - tool to edit environment block.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008,2009,2010 Free Software Foundation, Inc.
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
#include <grub/types.h>
#include <grub/emu/misc.h>
#include <grub/util/misc.h>
#include <grub/lib/envblk.h>
#include <grub/i18n.h>
#include <grub/emu/hostdisk.h>
#include <grub/util/install.h>
#include <grub/emu/getroot.h>
#include <grub/fs.h>
#include <grub/crypto.h>

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#include <argp.h>
#pragma GCC diagnostic error "-Wmissing-prototypes"
#pragma GCC diagnostic error "-Wmissing-declarations"


#include "progname.h"

#define DEFAULT_ENVBLK_PATH DEFAULT_DIRECTORY "/" GRUB_ENVBLK_DEFCFG

static struct argp_option options[] = {
  {0,        0, 0, OPTION_DOC, N_("Commands:"), 1},
  {"create", 0, 0, OPTION_DOC|OPTION_NO_USAGE,
   N_("Create a blank environment block file."), 0},
  {"list",   0, 0, OPTION_DOC|OPTION_NO_USAGE,
   N_("List the current variables."), 0},
  /* TRANSLATORS: "set" is a keyword. It's a summary of "set" subcommand.  */
  {N_("set [NAME=VALUE ...]"), 0, 0, OPTION_DOC|OPTION_NO_USAGE,
   N_("Set variables."), 0},
  /* TRANSLATORS: "unset" is a keyword. It's a summary of "unset" subcommand.  */
  {N_("unset [NAME ...]"),    0, 0, OPTION_DOC|OPTION_NO_USAGE,
   N_("Delete variables."), 0},

  {0,         0, 0, OPTION_DOC, N_("Options:"), -1},
  {"verbose", 'v', 0, 0, N_("print verbose messages."), 0},

  { 0, 0, 0, 0, 0, 0 }
};

/* Print the version information.  */
static void
print_version (FILE *stream, struct argp_state *state)
{
  fprintf (stream, "%s (%s) %s\n", program_name, PACKAGE_NAME, PACKAGE_VERSION);
}
void (*argp_program_version_hook) (FILE *, struct argp_state *) = print_version;

/* Set the bug report address */
const char *argp_program_bug_address = "<"PACKAGE_BUGREPORT">";

static error_t argp_parser (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
      case 'v':
        verbosity++;
        break;

      case ARGP_KEY_NO_ARGS:
        fprintf (stderr, "%s",
		 _("You need to specify at least one command.\n"));
        argp_usage (state);
        break;

      default:
        return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

#pragma GCC diagnostic ignored "-Wformat-nonliteral"

static char *
help_filter (int key, const char *text, void *input __attribute__ ((unused)))
{
  switch (key)
    {
      case ARGP_KEY_HELP_POST_DOC:
        return xasprintf (text, DEFAULT_ENVBLK_PATH, DEFAULT_ENVBLK_PATH);

      default:
        return (char *) text;
    }
}

#pragma GCC diagnostic error "-Wformat-nonliteral"

struct argp argp = {
  options, argp_parser, N_("FILENAME COMMAND"),
  "\n"N_("\
Tool to edit environment block.")
"\v"N_("\
If FILENAME is `-', the default value %s is used.\n\n\
There is no `delete' command; if you want to delete the whole environment\n\
block, use `rm %s'."),
  NULL, help_filter, NULL
};

struct fs_envblk_spec {
  const char *fs_name;
  int offset;
  int size;
} fs_envblk_spec[] = {
  { "btrfs", 256 * 1024, GRUB_DISK_SECTOR_SIZE },
  { NULL, 0, 0 }
};

struct fs_envblk {
  struct fs_envblk_spec *spec;
  const char *dev;
};

typedef struct fs_envblk_spec *fs_envblk_spec_t;
typedef struct fs_envblk *fs_envblk_t;

fs_envblk_t fs_envblk = NULL;

static int
read_envblk_fs (const char *varname, const char *value, void *hook_data)
{
  grub_envblk_t *p_envblk = (grub_envblk_t *)hook_data;

  if (!p_envblk || !fs_envblk)
    return 0;

  if (strcmp (varname, "env_block") == 0)
    {
      int off, sz;
      char *p;

      off = strtol (value, &p, 10);
      if (*p == '+')
	sz = strtol (p+1, &p, 10);

      if (*p == '\0')
	{
	  FILE *fp;
	  char *buf;

	  off <<= GRUB_DISK_SECTOR_BITS;
	  sz <<= GRUB_DISK_SECTOR_BITS;

	  fp = grub_util_fopen (fs_envblk->dev, "rb");
	  if (! fp)
	    grub_util_error (_("cannot open `%s': %s"), fs_envblk->dev,
				strerror (errno));


	  if (fseek (fp, off, SEEK_SET) < 0)
	    grub_util_error (_("cannot seek `%s': %s"), fs_envblk->dev,
				strerror (errno));

	  buf = xmalloc (sz);
	  if ((fread (buf, 1, sz, fp)) != sz)
	    grub_util_error (_("cannot read `%s': %s"), fs_envblk->dev,
				strerror (errno));

	  fclose (fp);

	  *p_envblk = grub_envblk_open (buf, sz);
	}
    }

  return 0;
}

static void
create_envblk_fs (void)
{
  FILE *fp;
  char *buf;
  const char *device;
  int offset, size;

  if (!fs_envblk)
    return;

  device = fs_envblk->dev;
  offset = fs_envblk->spec->offset;
  size = fs_envblk->spec->size;

  fp = grub_util_fopen (device, "r+b");
  if (! fp)
    grub_util_error (_("cannot open `%s': %s"), device, strerror (errno));

  buf = xmalloc (size);
  memcpy (buf, GRUB_ENVBLK_SIGNATURE, sizeof (GRUB_ENVBLK_SIGNATURE) - 1);
  memset (buf + sizeof (GRUB_ENVBLK_SIGNATURE) - 1, '#', size - sizeof (GRUB_ENVBLK_SIGNATURE) + 1);

  if (fseek (fp, offset, SEEK_SET) < 0)
    grub_util_error (_("cannot seek `%s': %s"), device, strerror (errno));

  if (fwrite (buf, 1, size, fp) != size)
    grub_util_error (_("cannot write to `%s': %s"), device, strerror (errno));

  grub_util_file_sync (fp);
  free (buf);
  fclose (fp);
}

static grub_envblk_t
open_envblk_fs (grub_envblk_t envblk)
{
  grub_envblk_t envblk_fs = NULL;
  char *val;
  int offset, size;

  if (!fs_envblk)
    return NULL;

  offset = fs_envblk->spec->offset;
  size = fs_envblk->spec->size;

  grub_envblk_iterate (envblk, &envblk_fs, read_envblk_fs);

  if (envblk_fs && grub_envblk_size (envblk_fs) == size)
    return envblk_fs;

  create_envblk_fs ();

  offset = offset >> GRUB_DISK_SECTOR_BITS;
  size =  (size + GRUB_DISK_SECTOR_SIZE - 1) >> GRUB_DISK_SECTOR_BITS;

  val = xasprintf ("%d+%d", offset, size);
  if (! grub_envblk_set (envblk, "env_block", val))
    grub_util_error ("%s", _("environment block too small"));
  grub_envblk_iterate (envblk, &envblk_fs, read_envblk_fs);
  free (val);

  return envblk_fs;
}

static grub_envblk_t
open_envblk_file (const char *name)
{
  FILE *fp;
  char *buf;
  long loc;
  size_t size;
  grub_envblk_t envblk;

  fp = grub_util_fopen (name, "rb");
  if (! fp)
    {
      /* Create the file implicitly.  */
      grub_util_create_envblk_file (name);
      fp = grub_util_fopen (name, "rb");
      if (! fp)
        grub_util_error (_("cannot open `%s': %s"), name,
			 strerror (errno));
    }

  if (fseek (fp, 0, SEEK_END) < 0)
    grub_util_error (_("cannot seek `%s': %s"), name,
		     strerror (errno));

  loc = ftell (fp);
  if (loc < 0)
    grub_util_error (_("cannot get file location `%s': %s"), name,
		     strerror (errno));

  size = (size_t) loc;

  if (fseek (fp, 0, SEEK_SET) < 0)
    grub_util_error (_("cannot seek `%s': %s"), name,
		     strerror (errno));

  buf = xmalloc (size);

  if (fread (buf, 1, size, fp) != size)
    grub_util_error (_("cannot read `%s': %s"), name,
		     strerror (errno));

  fclose (fp);

  envblk = grub_envblk_open (buf, size);
  if (! envblk)
    grub_util_error ("%s", _("invalid environment block"));

  return envblk;
}

static int
print_var (const char *varname, const char *value,
           void *hook_data __attribute__ ((unused)))
{
  printf ("%s=%s\n", varname, value);
  return 0;
}

static void
list_variables (const char *name)
{
  grub_envblk_t envblk;
  grub_envblk_t envblk_fs = NULL;

  envblk = open_envblk_file (name);
  grub_envblk_iterate (envblk, &envblk_fs, read_envblk_fs);
  grub_envblk_iterate (envblk, NULL, print_var);
  grub_envblk_close (envblk);
  if (envblk_fs)
    {
      grub_envblk_iterate (envblk_fs, NULL, print_var);
      grub_envblk_close (envblk_fs);
    }
}

static void
write_envblk (const char *name, grub_envblk_t envblk)
{
  FILE *fp;

  fp = grub_util_fopen (name, "wb");
  if (! fp)
    grub_util_error (_("cannot open `%s': %s"), name,
		     strerror (errno));

  if (fwrite (grub_envblk_buffer (envblk), 1, grub_envblk_size (envblk), fp)
      != grub_envblk_size (envblk))
    grub_util_error (_("cannot write to `%s': %s"), name,
		     strerror (errno));

  if (grub_util_file_sync (fp) < 0)
    grub_util_error (_("cannot sync `%s': %s"), name, strerror (errno));
  fclose (fp);
}

static void
write_envblk_fs (grub_envblk_t envblk)
{
  FILE *fp;
  const char *device;
  int offset, size;

  if (!fs_envblk)
    return;

  device = fs_envblk->dev;
  offset = fs_envblk->spec->offset;
  size = fs_envblk->spec->size;

  if (grub_envblk_size (envblk) > size)
    grub_util_error ("%s", _("environment block too small"));

  fp = grub_util_fopen (device, "r+b");

  if (! fp)
    grub_util_error (_("cannot open `%s': %s"), device, strerror (errno));

  if (fseek (fp, offset, SEEK_SET) < 0)
    grub_util_error (_("cannot seek `%s': %s"), device, strerror (errno));

  if (fwrite (grub_envblk_buffer (envblk), 1, grub_envblk_size (envblk), fp) != grub_envblk_size (envblk))
    grub_util_error (_("cannot write to `%s': %s"), device, strerror (errno));

  grub_util_file_sync (fp);
  fclose (fp);
}

static void
set_variables (const char *name, int argc, char *argv[])
{
  grub_envblk_t envblk;

  envblk = open_envblk_file (name);
  while (argc)
    {
      char *p;

      p = strchr (argv[0], '=');
      if (! p)
        grub_util_error (_("invalid parameter %s"), argv[0]);

      *(p++) = 0;

      if ((strcmp (argv[0], "next_entry") == 0 ||
	  strcmp (argv[0], "health_checker_flag") == 0) && fs_envblk)
	{
	  grub_envblk_t envblk_fs;
	  envblk_fs = open_envblk_fs (envblk);
	  if (!envblk_fs)
	    grub_util_error ("%s", _("can't open fs environment block"));
	  if (! grub_envblk_set (envblk_fs, argv[0], p))
	    grub_util_error ("%s", _("environment block too small"));
	  write_envblk_fs (envblk_fs);
	  grub_envblk_close (envblk_fs);
	}
      else if (strcmp (argv[0], "env_block") == 0)
	{
	  grub_util_warn ("can't set env_block as it's read-only");
	}
      else
	{
	  if (! grub_envblk_set (envblk, argv[0], p))
	    grub_util_error ("%s", _("environment block too small"));
	}

      argc--;
      argv++;
    }

  write_envblk (name, envblk);
  grub_envblk_close (envblk);

}

static void
unset_variables (const char *name, int argc, char *argv[])
{
  grub_envblk_t envblk;
  grub_envblk_t envblk_fs;

  envblk = open_envblk_file (name);

  envblk_fs = NULL;
  if (fs_envblk)
    envblk_fs = open_envblk_fs (envblk);

  while (argc)
    {
      grub_envblk_delete (envblk, argv[0]);

      if (envblk_fs)
	grub_envblk_delete (envblk_fs, argv[0]);

      argc--;
      argv++;
    }

  write_envblk (name, envblk);
  grub_envblk_close (envblk);

  if (envblk_fs)
    {
      write_envblk_fs (envblk_fs);
      grub_envblk_close (envblk_fs);
    }
}

int have_abstraction = 0;
static void
probe_abstraction (grub_disk_t disk)
{
  if (disk->partition == NULL)
    grub_util_info ("no partition map found for %s", disk->name);

  if (disk->dev->id == GRUB_DISK_DEVICE_DISKFILTER_ID ||
      disk->dev->id == GRUB_DISK_DEVICE_CRYPTODISK_ID)
    {
      have_abstraction = 1;
    }
}

static fs_envblk_t
probe_fs_envblk (fs_envblk_spec_t spec)
{
  char **grub_devices;
  char **curdev, **curdrive;
  size_t ndev = 0;
  char **grub_drives;
  grub_device_t grub_dev = NULL;
  grub_fs_t grub_fs;
  const char *fs_envblk_device;

#ifdef __s390x__
  return NULL;
#endif

  grub_util_biosdisk_init (DEFAULT_DEVICE_MAP);
  grub_init_all ();
  grub_gcry_init_all ();

  grub_lvm_fini ();
  grub_mdraid09_fini ();
  grub_mdraid1x_fini ();
  grub_diskfilter_fini ();
  grub_diskfilter_init ();
  grub_mdraid09_init ();
  grub_mdraid1x_init ();
  grub_lvm_init ();

  grub_devices = grub_guess_root_devices (DEFAULT_DIRECTORY);

  if (!grub_devices || !grub_devices[0])
    grub_util_error (_("cannot find a device for %s (is /dev mounted?)"), DEFAULT_DIRECTORY);

  fs_envblk_device = grub_devices[0];

  for (curdev = grub_devices; *curdev; curdev++)
    {
      grub_util_pull_device (*curdev);
      ndev++;
    }

  grub_drives = xcalloc ((ndev + 1), sizeof (grub_drives[0]));

  for (curdev = grub_devices, curdrive = grub_drives; *curdev; curdev++,
       curdrive++)
    {
      *curdrive = grub_util_get_grub_dev (*curdev);
      if (! *curdrive)
	grub_util_error (_("cannot find a GRUB drive for %s.  Check your device.map"),
			 *curdev);
    }
  *curdrive = 0;

  grub_dev = grub_device_open (grub_drives[0]);
  if (! grub_dev)
    grub_util_error ("%s", grub_errmsg);

  grub_fs = grub_fs_probe (grub_dev);
  if (! grub_fs)
    grub_util_error ("%s", grub_errmsg);

  if (grub_dev->disk)
    {
      probe_abstraction (grub_dev->disk);
    }
  for (curdrive = grub_drives + 1; *curdrive; curdrive++)
    {
      grub_device_t dev = grub_device_open (*curdrive);
      if (!dev)
	continue;
      if (dev->disk)
	probe_abstraction (dev->disk);
      grub_device_close (dev);
    }

  free (grub_drives);
  grub_device_close (grub_dev);
  grub_gcry_fini_all ();
  grub_fini_all ();
  grub_util_biosdisk_fini ();

  fs_envblk_spec_t p;

  for (p = spec; p->fs_name; p++)
    {
      if (strcmp (grub_fs->name, p->fs_name) == 0 && !have_abstraction)
	{
	  if (p->offset % GRUB_DISK_SECTOR_SIZE == 0 &&
	      p->size % GRUB_DISK_SECTOR_SIZE == 0)
	    {
	      fs_envblk = xmalloc (sizeof (fs_envblk_t));
	      fs_envblk->spec = p;
	      fs_envblk->dev = strdup(fs_envblk_device);
	      return fs_envblk;
	    }
	}
    }

  return NULL;
}


int
main (int argc, char *argv[])
{
  const char *filename;
  char *command;
  int curindex, arg_count;

  grub_util_host_init (&argc, &argv);

  /* Parse our arguments */
  if (argp_parse (&argp, argc, argv, 0, &curindex, 0) != 0)
    {
      fprintf (stderr, "%s", _("Error in parsing command line arguments\n"));
      exit(1);
    }

  arg_count = argc - curindex;

  if (arg_count == 1)
    {
      filename = DEFAULT_ENVBLK_PATH;
      command  = argv[curindex++];
    }
  else
    {
      filename = argv[curindex++];
      if (strcmp (filename, "-") == 0)
        filename = DEFAULT_ENVBLK_PATH;
      command  = argv[curindex++];
    }

  if (strcmp (filename, DEFAULT_ENVBLK_PATH) == 0)
    fs_envblk = probe_fs_envblk (fs_envblk_spec);

  if (strcmp (command, "create") == 0)
    grub_util_create_envblk_file (filename);
  else if (strcmp (command, "list") == 0)
    list_variables (filename);
  else if (strcmp (command, "set") == 0)
    set_variables (filename, argc - curindex, argv + curindex);
  else if (strcmp (command, "unset") == 0)
    unset_variables (filename, argc - curindex, argv + curindex);
  else
    {
      char *program = xstrdup(program_name);
      fprintf (stderr, _("Unknown command `%s'.\n"), command);
      argp_help (&argp, stderr, ARGP_HELP_STD_USAGE, program);
      free(program);
      exit(1);
    }

  return 0;
}
