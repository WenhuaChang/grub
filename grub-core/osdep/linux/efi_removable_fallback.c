/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013 Free Software Foundation, Inc.
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

#include <grub/util/install.h>
#include <grub/emu/exec.h>
#include <grub/emu/misc.h>
#include <string.h>

static char *
get_dmi_id (const char *id)
{
  FILE *fp;
  char *buf = NULL;
  size_t len = 0;

  char *dmi_entry;

  dmi_entry = grub_util_path_concat (2, "/sys/class/dmi/id", id);

  fp = grub_util_fopen (dmi_entry, "r");
  if (!fp)
    {
      free (dmi_entry);
      return NULL;
    }

  if (getline (&buf, &len, fp) == -1)
    {
      fclose (fp);
      free (dmi_entry);
      return NULL;
    }

  fclose (fp);
  free (dmi_entry);
  return buf;
}


static struct dmi {
  const char *id;
  const char *val;
} azure_dmi [3] = {
  {"bios_vendor", "Microsoft Corporation"},
  {"product_name", "Virtual Machine"},
  {"sys_vendor", "Microsoft Corporation"},
};

static int
is_azure (void)
{
  int i;
  int n = sizeof (azure_dmi) / sizeof (struct dmi);

  for (i = 0; i < n; ++i)
    {
      char *val;

      val = get_dmi_id (azure_dmi[i].id);
      if (!val)
	break;
      if (strncmp (val, azure_dmi[i].val, strlen (azure_dmi[i].val)) != 0)
	{
	  free (val);
	  break;
	}
      free (val);
    }

  return (i == n) ? 1 : 0;
}

static int
guess_shim_installed (const char *instdir)
{
  const char *shim[] = {"fallback.efi", "MokManager.efi", NULL};
  const char **s;

  for (s = shim; *s ; ++s)
    {
      char *p = grub_util_path_concat (2, instdir, *s);

      if (access (p, F_OK) == 0)
	{
	  free (p);
	  return 1;
	}
      free (p);
    }

  return 0;
}

const char *
grub_install_efi_removable_fallback (const char *efidir, enum grub_install_plat platform)
{
  char *instdir;

  if (!is_azure ())
    return NULL;

  instdir = grub_util_path_concat (3, efidir, "EFI", "BOOT");

  if (guess_shim_installed (instdir))
    {
      grub_util_info ("skip removable fallback occupied by shim");
      return NULL;
    }

  free (instdir);

  switch (platform)
    {
    case GRUB_INSTALL_PLATFORM_I386_EFI:
      return "BOOTIA32.EFI";
    case GRUB_INSTALL_PLATFORM_X86_64_EFI:
      return "BOOTX64.EFI";
    case GRUB_INSTALL_PLATFORM_IA64_EFI:
      return "BOOTIA64.EFI";
    case GRUB_INSTALL_PLATFORM_ARM_EFI:
      return "BOOTARM.EFI";
    case GRUB_INSTALL_PLATFORM_ARM64_EFI:
      return "BOOTAA64.EFI";
    case GRUB_INSTALL_PLATFORM_RISCV32_EFI:
      return "BOOTRISCV32.EFI";
    case GRUB_INSTALL_PLATFORM_RISCV64_EFI:
      return "BOOTRISCV64.EFI";
    default:
      grub_util_error ("%s", _("You've found a bug"));
      break;
    }
  return NULL;
}

