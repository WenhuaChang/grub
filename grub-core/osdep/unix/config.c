/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2004,2006,2007,2008,2009,2010,2011,2012,2013  Free Software Foundation, Inc.
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

#include <grub/emu/hostdisk.h>
#include <grub/emu/exec.h>
#include <grub/emu/config.h>
#include <grub/util/install.h>
#include <grub/util/misc.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdlib.h>

const char *
grub_util_get_config_filename (void)
{
  static char *value = NULL;
  if (!value)
    value = grub_util_path_concat (3, GRUB_SYSCONFDIR,
				   "default", "grub");
  return value;
}

const char *
grub_util_get_pkgdatadir (void)
{
  const char *ret = getenv ("pkgdatadir");
  if (ret)
    return ret;
  return GRUB_DATADIR "/" PACKAGE;
}

const char *
grub_util_get_pkglibdir (void)
{
  return GRUB_LIBDIR "/" PACKAGE;
}

const char *
grub_util_get_localedir (void)
{
  return LOCALEDIR;
}

#ifdef __linux__
static char *
os_release_get_val (const char *buf, const char *key)
{
  const char *ptr = buf;
  char *ret;

  while (*ptr && grub_isspace(*ptr))
    ptr++;

  if (*ptr == '#')
    return NULL;

  if (grub_strncmp (ptr, key, grub_strlen (key)) != 0)
    return NULL;

  ptr += grub_strlen (key);
  if (*ptr++ != '=' || *ptr == '\0')
    return NULL;

  if (*ptr == '"' || *ptr == '\'')
    {
      char c = *ptr;
      int i = 0;
      char *tmp, *ptmp;

      if (*++ptr == '\0')
	return NULL;

      tmp = grub_strdup (ptr);
      if ((ptmp = grub_strrchr (tmp, c)))
	*ptmp = '\0';

      ret = malloc (grub_strlen (tmp) + 1);
      ptmp = tmp;
      while (*ptmp)
	{
	  if (*ptmp != '\\' || *(ptmp + 1) != c)
	    ret[i++] = *ptmp;
	  ++ptmp;
	}

      grub_free (tmp);
      ret[i] = '\0';
    }
  else
    {
      char *pret;

      ret = grub_strdup (ptr);
      if ((pret = grub_strchr (ret, ' ')))
	*pret = '\0';
    }

  return ret;
}

static char*
grub_util_default_distributor (void)
{
  char *cfgfile;
  char buf[1024];
  FILE *fp = NULL;
  char *os_pretty_name = NULL;
  char *os_name = NULL;
  char *os_version = NULL;

  cfgfile = grub_util_path_concat (2, GRUB_SYSCONFDIR, "os-release");
  if (!grub_util_is_regular (cfgfile))
    {
      grub_free (cfgfile);
      return NULL;
    }

  fp = grub_util_fopen (cfgfile, "r");

  if (!fp)
    {
      grub_util_warn (_("cannot open configuration file `%s': %s"),
		      cfgfile, strerror (errno));
      grub_free (cfgfile);
      return NULL;
    }

  grub_free (cfgfile);

  while (fgets (buf, sizeof (buf), fp))
    {
      if (buf[grub_strlen(buf) - 1] == '\n')
	buf[grub_strlen(buf) - 1] = '\0';

      if (!os_pretty_name
	  && (os_pretty_name = os_release_get_val (buf, "PRETTY_NAME")))
	continue;
      if (!os_name
	  && (os_name = os_release_get_val (buf, "NAME")))
	continue;
      if (!os_version
	  && (os_version = os_release_get_val (buf, "VERSION")))
	continue;
      if (os_pretty_name && os_name && os_version)
	break;
    }

  fclose (fp);

  if (os_name && grub_strncmp (os_name, "openSUSE Tumbleweed", sizeof ("openSUSE Tumbleweed") - 1) == 0)
    {
      grub_free (os_name);
      if (os_version)
	grub_free (os_version);

      return os_pretty_name;
    }
  else if (os_name && os_version)
    {
      char *os_name_version;

      os_name_version = grub_xasprintf ("%s %s", os_name, os_version);

      grub_free (os_name);
      grub_free (os_version);
      if (os_pretty_name)
	grub_free (os_pretty_name);

      return os_name_version;
    }

  if (os_pretty_name)
    grub_free (os_pretty_name);
  if (os_version)
    grub_free (os_version);

  return os_name;
}
#endif

void
grub_util_load_config (struct grub_util_config *cfg)
{
  pid_t pid;
  const char *argv[4];
  char *script, *ptr;
  const char *cfgfile, *iptr;
  FILE *f = NULL;
  int fd;
  const char *v;

  memset (cfg, 0, sizeof (*cfg));

  v = getenv ("GRUB_ENABLE_CRYPTODISK");
  if (v && v[0] == 'y' && v[1] == '\0')
    cfg->is_cryptodisk_enabled = 1;

  v = getenv ("GRUB_DISTRIBUTOR");
  if (v)
    cfg->grub_distributor = xstrdup (v);

  cfgfile = grub_util_get_config_filename ();
  if (!grub_util_is_regular (cfgfile))
    return;

  argv[0] = "sh";
  argv[1] = "-c";

  script = xcalloc (4, strlen (cfgfile) + 300);

  ptr = script;
  memcpy (ptr, ". '", 3);
  ptr += 3;
  for (iptr = cfgfile; *iptr; iptr++)
    {
      if (*iptr == '\\')
	{
	  memcpy (ptr, "'\\''", 4);
	  ptr += 4;
	  continue;
	}
      *ptr++ = *iptr;
    }

  strcpy (ptr, "'; printf \"GRUB_ENABLE_CRYPTODISK=%s\\nGRUB_DISTRIBUTOR=%s\\n\" "
	  "\"$GRUB_ENABLE_CRYPTODISK\" \"$GRUB_DISTRIBUTOR\"");

  argv[2] = script;
  argv[3] = '\0';

  pid = grub_util_exec_pipe (argv, &fd);
  if (pid)
    f = fdopen (fd, "r");
  if (f)
    {
      grub_util_parse_config (f, cfg, 1);
      fclose (f);
    }
  if (pid)
    {
      close (fd);
      waitpid (pid, NULL, 0);
    }
  if (f)
    {
#ifdef __linux__
      if (!cfg->grub_distributor || cfg->grub_distributor[0] == '\0')
	{
	  if (cfg->grub_distributor)
	    grub_free (cfg->grub_distributor);
	  cfg->grub_distributor = grub_util_default_distributor ();
	}
#endif
      return;
    }

  f = grub_util_fopen (cfgfile, "r");
  if (f)
    {
      grub_util_parse_config (f, cfg, 0);
      fclose (f);
    }
  else
    grub_util_warn (_("cannot open configuration file `%s': %s"),
		    cfgfile, strerror (errno));

#ifdef __linux__
  if (!cfg->grub_distributor || cfg->grub_distributor[0] == '\0')
    {
      if (cfg->grub_distributor)
	grub_free (cfg->grub_distributor);
      cfg->grub_distributor = grub_util_default_distributor ();
    }
#endif
}
