/* chainloader.c - boot another boot loader */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2004,2006,2007,2008  Free Software Foundation, Inc.
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

/* TODO: support load options.  */

#include <grub/loader.h>
#include <grub/file.h>
#include <grub/err.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/misc.h>
#include <grub/charset.h>
#include <grub/mm.h>
#include <grub/types.h>
#include <grub/dl.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/disk.h>
#include <grub/command.h>
#include <grub/i18n.h>
#include <grub/net.h>
#if defined (__i386__) || defined (__x86_64__)
#include <grub/macho.h>
#include <grub/i386/macho.h>
#endif

#ifdef __x86_64__
#define SUPPORT_SECURE_BOOT
#endif

#ifdef SUPPORT_SECURE_BOOT
#include <grub/efi/pe32.h>
#include <grub/efi/sb.h>
#endif

GRUB_MOD_LICENSE ("GPLv3+");

static grub_dl_t my_mod;

static grub_efi_physical_address_t address;
static grub_efi_uintn_t pages;
static grub_ssize_t fsize;
static grub_efi_device_path_t *file_path;
static grub_efi_handle_t image_handle;
static grub_efi_char16_t *cmdline;
static grub_ssize_t cmdline_len;
static grub_efi_handle_t dev_handle;

#ifdef SUPPORT_SECURE_BOOT
static grub_efi_boolean_t debug_secureboot = 0;
static grub_efi_status_t (*entry_point) (grub_efi_handle_t image_handle,  grub_efi_system_table_t *system_table);
#endif

static grub_err_t
grub_chainloader_unload (void)
{
  grub_efi_boot_services_t *b;

  b = grub_efi_system_table->boot_services;
  efi_call_1 (b->unload_image, image_handle);
  efi_call_2 (b->free_pages, address, pages);

  grub_free (file_path);
  grub_free (cmdline);
  cmdline = 0;
  file_path = 0;
  dev_handle = 0;

  grub_dl_unref (my_mod);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_chainloader_boot (void)
{
  grub_efi_boot_services_t *b;
  grub_efi_status_t status;
  grub_efi_uintn_t exit_data_size;
  grub_efi_char16_t *exit_data = NULL;

  b = grub_efi_system_table->boot_services;
  status = efi_call_3 (b->start_image, image_handle, &exit_data_size, &exit_data);
  if (status != GRUB_EFI_SUCCESS)
    {
      if (exit_data)
	{
	  char *buf;

	  buf = grub_malloc (exit_data_size * 4 + 1);
	  if (buf)
	    {
	      *grub_utf16_to_utf8 ((grub_uint8_t *) buf,
				   exit_data, exit_data_size) = 0;

	      grub_error (GRUB_ERR_BAD_OS, "%s", buf);
	      grub_free (buf);
	    }
	}
      else
	grub_error (GRUB_ERR_BAD_OS, "unknown error");
    }

  if (exit_data)
    efi_call_1 (b->free_pool, exit_data);

  grub_loader_unset ();

  return grub_errno;
}

static grub_err_t
copy_file_path (grub_efi_file_path_device_path_t *fp,
		const char *str, grub_efi_uint16_t len)
{
  grub_efi_char16_t *p, *path_name;
  grub_efi_uint16_t size;

  fp->header.type = GRUB_EFI_MEDIA_DEVICE_PATH_TYPE;
  fp->header.subtype = GRUB_EFI_FILE_PATH_DEVICE_PATH_SUBTYPE;

  path_name = grub_calloc (len, GRUB_MAX_UTF16_PER_UTF8 * sizeof (*path_name));
  if (!path_name)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "failed to allocate path buffer");

  size = grub_utf8_to_utf16 (path_name, len * GRUB_MAX_UTF16_PER_UTF8,
			     (const grub_uint8_t *) str, len, 0);
  for (p = path_name; p < path_name + size; p++)
    if (*p == '/')
      *p = '\\';

  grub_memcpy (fp->path_name, path_name, size * sizeof (*fp->path_name));
  /* File Path is NULL terminated */
  fp->path_name[size++] = '\0';
  fp->header.length = size * sizeof (grub_efi_char16_t) + sizeof (*fp);
  grub_free (path_name);
  return GRUB_ERR_NONE;
}

static grub_efi_device_path_t *
make_file_path (grub_efi_device_path_t *dp, const char *filename)
{
  char *dir_start;
  char *dir_end;
  grub_size_t size;
  grub_efi_device_path_t *d;

  dir_start = grub_strchr (filename, ')');
  if (! dir_start)
    dir_start = (char *) filename;
  else
    dir_start++;

  dir_end = grub_strrchr (dir_start, '/');
  if (! dir_end)
    {
      grub_error (GRUB_ERR_BAD_FILENAME, "invalid EFI file path");
      return 0;
    }

  size = 0;
  d = dp;
  while (d)
    {
      grub_size_t len = GRUB_EFI_DEVICE_PATH_LENGTH (d);

      if (len < 4)
	{
	  grub_error (GRUB_ERR_OUT_OF_RANGE,
		      "malformed EFI Device Path node has length=%" PRIuGRUB_SIZE, len);
	  return NULL;
	}

      size += len;
      if ((GRUB_EFI_END_ENTIRE_DEVICE_PATH (d)))
	break;
      d = GRUB_EFI_NEXT_DEVICE_PATH (d);
    }

  /* File Path is NULL terminated. Allocate space for 2 extra characters */
  /* FIXME why we split path in two components? */
  file_path = grub_malloc (size
			   + ((grub_strlen (dir_start) + 2)
			      * GRUB_MAX_UTF16_PER_UTF8
			      * sizeof (grub_efi_char16_t))
			   + sizeof (grub_efi_file_path_device_path_t) * 2);
  if (! file_path)
    return 0;

  grub_memcpy (file_path, dp, size);

  /* Fill the file path for the directory.  */
  d = (grub_efi_device_path_t *) ((char *) file_path
				  + ((char *) d - (char *) dp));
  grub_efi_print_device_path (d);
  if (copy_file_path ((grub_efi_file_path_device_path_t *) d,
		      dir_start, dir_end - dir_start) != GRUB_ERR_NONE)
    {
 fail:
      grub_free (file_path);
      return 0;
    }

  /* Fill the file path for the file.  */
  d = GRUB_EFI_NEXT_DEVICE_PATH (d);
  if (copy_file_path ((grub_efi_file_path_device_path_t *) d,
		      dir_end + 1, grub_strlen (dir_end + 1)) != GRUB_ERR_NONE)
    goto fail;

  /* Fill the end of device path nodes.  */
  d = GRUB_EFI_NEXT_DEVICE_PATH (d);
  d->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
  d->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
  d->length = sizeof (*d);

  return file_path;
}

#ifdef SUPPORT_SECURE_BOOT
#define SHIM_LOCK_GUID \
  { 0x605dab50, 0xe046, 0x4300, {0xab, 0xb6, 0x3d, 0xd8, 0x10, 0xdd, 0x8b, 0x23} }

struct grub_pe32_header_no_msdos_stub
{
  char signature[GRUB_PE32_SIGNATURE_SIZE];
  struct grub_pe32_coff_header coff_header;
  struct grub_pe64_optional_header optional_header;
};

struct pe_coff_loader_image_context
{
  grub_efi_uint64_t image_address;
  grub_efi_uint64_t image_size;
  grub_efi_uint64_t entry_point;
  grub_efi_uintn_t size_of_headers;
  grub_efi_uint16_t image_type;
  grub_efi_uint16_t number_of_sections;
  struct grub_pe32_section_table *first_section;
  struct grub_pe32_data_directory *reloc_dir;
  struct grub_pe32_data_directory *sec_dir;
  grub_efi_uint64_t number_of_rva_and_sizes;
  struct grub_pe32_header_no_msdos_stub *pe_hdr;
};

typedef struct pe_coff_loader_image_context pe_coff_loader_image_context_t;

struct grub_efi_shim_lock
{
  grub_efi_status_t (*verify)(void *buffer,
                              grub_efi_uint32_t size);
  grub_efi_status_t (*hash)(void *data,
                            grub_efi_int32_t datasize,
                            pe_coff_loader_image_context_t *context,
                            grub_efi_uint8_t *sha256hash,
                            grub_efi_uint8_t *sha1hash);
  grub_efi_status_t (*context)(void *data,
                               grub_efi_uint32_t size,
                               pe_coff_loader_image_context_t *context);
};

typedef struct grub_efi_shim_lock grub_efi_shim_lock_t;

static grub_efi_boolean_t
grub_secure_validate (void *data, grub_efi_uint32_t size)
{
  grub_efi_guid_t guid = SHIM_LOCK_GUID;
  grub_efi_shim_lock_t *shim_lock;

  shim_lock = grub_efi_locate_protocol (&guid, NULL);

  if (!shim_lock)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, "no shim lock protocol");
      return 0;
    }

  if (shim_lock->verify (data, size) == GRUB_EFI_SUCCESS)
    {
      grub_dprintf ("chain", "verify success\n");
      return 1;
    }

  grub_error (GRUB_ERR_BAD_ARGUMENT, "verify failed");
  return 0;
}

static grub_efi_boolean_t
read_header (void *data, grub_efi_uint32_t size, pe_coff_loader_image_context_t *context)
{
  grub_efi_guid_t guid = SHIM_LOCK_GUID;
  grub_efi_shim_lock_t *shim_lock;
  grub_efi_status_t status;

  shim_lock = grub_efi_locate_protocol (&guid, NULL);

  if (!shim_lock)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, "no shim lock protocol");
      return 0;
    }

  status = shim_lock->context (data, size, context);

  if (status == GRUB_EFI_SUCCESS)
    {
      grub_dprintf ("chain", "context success\n");
      return 1;
    }

  switch (status)
    {
      case GRUB_EFI_UNSUPPORTED:
      grub_error (GRUB_ERR_BAD_ARGUMENT, "context error unsupported");
      break;
      case GRUB_EFI_INVALID_PARAMETER:
      grub_error (GRUB_ERR_BAD_ARGUMENT, "context error invalid parameter");
      break;
      default:
      grub_error (GRUB_ERR_BAD_ARGUMENT, "context error code");
      break;
    }

  return 0;
}

static void*
image_address (void *image, grub_efi_uint64_t sz, grub_efi_uint64_t adr)
{
  if (adr > sz)
    return NULL;

  return ((grub_uint8_t*)image + adr);
}

static grub_efi_status_t
relocate_coff (pe_coff_loader_image_context_t *context, void *data)
{
  struct grub_pe32_data_directory *reloc_base, *reloc_base_end;
  grub_efi_uint64_t adjust;
  grub_efi_uint16_t *reloc, *reloc_end;
  char *fixup, *fixup_base, *fixup_data = NULL;
  grub_efi_uint16_t *fixup_16;
  grub_efi_uint32_t *fixup_32;
  grub_efi_uint64_t *fixup_64;

  grub_efi_uint64_t size = context->image_size;
  void *image_end = (char *)data + size;

  context->pe_hdr->optional_header.image_base = (grub_uint64_t)data;

  if (context->number_of_rva_and_sizes <= 5 || context->reloc_dir->size == 0)
    {
      grub_dprintf ("chain", "no need to reloc, we are done\n");
      return GRUB_EFI_SUCCESS;
    }

  reloc_base = image_address (data, size, context->reloc_dir->rva);
  reloc_base_end = image_address (data, size, context->reloc_dir->rva + context->reloc_dir->size -1);

  grub_dprintf ("chain", "reloc_base %p reloc_base_end %p\n", reloc_base, reloc_base_end);

  if (!reloc_base || !reloc_base_end)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, "Reloc table overflows binary");
      return GRUB_EFI_UNSUPPORTED;
    }

  adjust = (grub_uint64_t)data - context->image_address;

  while (reloc_base < reloc_base_end)
    {
      reloc = (grub_uint16_t *)((char*)reloc_base + sizeof (struct grub_pe32_data_directory));
      reloc_end = (grub_uint16_t *)((char*)reloc_base + reloc_base->size);

      if ((void *)reloc_end < data || (void *)reloc_end > image_end)
        {
          grub_error (GRUB_ERR_BAD_ARGUMENT, "Reloc table overflows binary");
          return GRUB_EFI_UNSUPPORTED;
        }

      fixup_base = image_address(data, size, reloc_base->rva);

      if (!fixup_base)
        {
          grub_error (GRUB_ERR_BAD_ARGUMENT, "Invalid fixupbase");
          return GRUB_EFI_UNSUPPORTED;
        }

      while (reloc < reloc_end)
        {
          fixup = fixup_base + (*reloc & 0xFFF);
          switch ((*reloc) >> 12)
            {
              case GRUB_PE32_REL_BASED_ABSOLUTE:
                break;
              case GRUB_PE32_REL_BASED_HIGH:
                fixup_16 = (grub_uint16_t *)fixup;
                *fixup_16 = (grub_uint16_t) (*fixup_16 + ((grub_uint16_t)((grub_uint32_t)adjust >> 16)));
                if (fixup_data != NULL)
                  {
                    *(grub_uint16_t *) fixup_data = *fixup_16;
                    fixup_data = fixup_data + sizeof (grub_uint16_t);
                  }
                break;
              case GRUB_PE32_REL_BASED_LOW:
                fixup_16 = (grub_uint16_t *)fixup;
                *fixup_16 = (grub_uint16_t) (*fixup_16 + (grub_uint16_t)adjust );
                if (fixup_data != NULL)
                  {
                    *(grub_uint16_t *) fixup_data = *fixup_16;
                    fixup_data = fixup_data + sizeof (grub_uint16_t);
                  }
                break;
              case GRUB_PE32_REL_BASED_HIGHLOW:
                fixup_32 = (grub_uint32_t *)fixup;
                *fixup_32 = *fixup_32 + (grub_uint32_t)adjust;
                if (fixup_data != NULL)
                  {
                    fixup_data = (char *)ALIGN_UP ((grub_addr_t)fixup_data, sizeof (grub_uint32_t));
                    *(grub_uint32_t *) fixup_data = *fixup_32;
                    fixup_data += sizeof (grub_uint32_t);
                  }
                break;
              case GRUB_PE32_REL_BASED_DIR64:
                fixup_64 = (grub_uint64_t *)fixup;
                *fixup_64 = *fixup_64 + (grub_uint64_t)adjust;
                if (fixup_data != NULL)
                  {
                    fixup_data = (char *)ALIGN_UP ((grub_addr_t)fixup_data, sizeof (grub_uint64_t));
                    *(grub_uint64_t *) fixup_data = *fixup_64;
                    fixup_data += sizeof (grub_uint64_t);
                  }
                break;
              default:
                grub_error (GRUB_ERR_BAD_ARGUMENT, "unknown relocation");
                return GRUB_EFI_UNSUPPORTED;
            }
          reloc += 1;
        }
      reloc_base = (struct grub_pe32_data_directory *)reloc_end;
    }

  return GRUB_EFI_SUCCESS;
}

static grub_efi_device_path_t *
grub_efi_get_media_file_path (grub_efi_device_path_t *dp)
{
  while (1)
    {
      grub_efi_uint8_t type = GRUB_EFI_DEVICE_PATH_TYPE (dp);
      grub_efi_uint8_t subtype = GRUB_EFI_DEVICE_PATH_SUBTYPE (dp);

      if (type == GRUB_EFI_END_DEVICE_PATH_TYPE)
        break;
      else if (type == GRUB_EFI_MEDIA_DEVICE_PATH_TYPE
            && subtype == GRUB_EFI_FILE_PATH_DEVICE_PATH_SUBTYPE)
      return dp;

      dp = GRUB_EFI_NEXT_DEVICE_PATH (dp);
    }

    return NULL;
}

static grub_efi_boolean_t
handle_image (void *data, grub_efi_uint32_t datasize)
{
  grub_efi_boot_services_t *b;
  grub_efi_loaded_image_t *li, li_bak;
  grub_efi_status_t efi_status;
  char *buffer = NULL;
  char *buffer_aligned = NULL;
  grub_efi_uint32_t i, size;
  struct grub_pe32_section_table *section;
  char *base, *end;
  pe_coff_loader_image_context_t context;
  grub_uint32_t section_alignment;
  grub_uint32_t buffer_size;

  b = grub_efi_system_table->boot_services;

  if (read_header (data, datasize, &context))
    {
      grub_dprintf ("chain", "Succeed to read header\n");
    }
  else
    {
      grub_dprintf ("chain", "Failed to read header\n");
      goto error_exit;
    }

  section_alignment = context.pe_hdr->optional_header.section_alignment;
  buffer_size = context.image_size + section_alignment;

  efi_status = efi_call_3 (b->allocate_pool, GRUB_EFI_LOADER_DATA,
			   buffer_size, &buffer);

  if (efi_status != GRUB_EFI_SUCCESS)
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
      goto error_exit;
    }

  buffer_aligned = (char *)ALIGN_UP ((grub_addr_t)buffer, section_alignment);

  grub_memcpy (buffer_aligned, data, context.size_of_headers);

  section = context.first_section;
  for (i = 0; i < context.number_of_sections; i++)
    {
      size = section->virtual_size;
      if (size > section->raw_data_size)
        size = section->raw_data_size;

      base = image_address (buffer_aligned, context.image_size, section->virtual_address);
      end = image_address (buffer_aligned, context.image_size, section->virtual_address + size - 1);

      if (!base || !end)
        {
          grub_error (GRUB_ERR_BAD_ARGUMENT, "Invalid section size");
          goto error_exit;
        }

      if (section->raw_data_size > 0)
        grub_memcpy (base, (grub_efi_uint8_t*)data + section->raw_data_offset, size);

      if (size < section->virtual_size)
        grub_memset (base + size, 0, section->virtual_size - size);

      grub_dprintf ("chain", "copied section %s\n", section->name);
      section += 1;
    }

  efi_status = relocate_coff (&context, buffer_aligned);

  if (efi_status != GRUB_EFI_SUCCESS)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, "relocation failed");
      goto error_exit;
    }

  entry_point = image_address (buffer_aligned, context.image_size, context.entry_point);

  if (!entry_point)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid entry point");
      goto error_exit;
    }

  li = grub_efi_get_loaded_image (grub_efi_image_handle);
  if (!li)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, "no loaded image available");
      goto error_exit;
    }

  grub_memcpy (&li_bak, li, sizeof (grub_efi_loaded_image_t));
  li->image_base = buffer_aligned;
  li->image_size = context.image_size;
  li->load_options = cmdline;
  li->load_options_size = cmdline_len;
  li->file_path = grub_efi_get_media_file_path (file_path);
  li->device_handle = dev_handle;
  if (li->file_path)
    {
      grub_printf ("file path: ");
      grub_efi_print_device_path (li->file_path);
    }
  else
    {
      grub_error (GRUB_ERR_UNKNOWN_DEVICE, "no matching file path found");
      goto error_exit;
    }

  efi_status = efi_call_2 (entry_point, grub_efi_image_handle, grub_efi_system_table);

  grub_memcpy (li, &li_bak, sizeof (grub_efi_loaded_image_t));
  efi_status = efi_call_1 (b->free_pool, buffer);

  return 1;

error_exit:
  if (buffer)
      efi_call_1 (b->free_pool, buffer);

  return 0;

}

static grub_err_t
grub_secureboot_chainloader_unload (void)
{
  grub_efi_boot_services_t *b;

  b = grub_efi_system_table->boot_services;
  efi_call_2 (b->free_pages, address, pages);
  grub_free (file_path);
  grub_free (cmdline);
  cmdline = 0;
  file_path = 0;
  dev_handle = 0;

  grub_dl_unref (my_mod);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_secureboot_chainloader_boot (void)
{
  handle_image ((void *)address, fsize);
  grub_loader_unset ();
  return grub_errno;
}
#endif

static grub_err_t
grub_cmd_chainloader (grub_command_t cmd __attribute__ ((unused)),
		      int argc, char *argv[])
{
  grub_file_t file = 0;
  grub_efi_status_t status;
  grub_efi_boot_services_t *b;
  grub_device_t dev = 0;
  grub_efi_device_path_t *dp = 0;
  grub_efi_loaded_image_t *loaded_image;
  char *filename;
  void *boot_image = 0;

  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));
  filename = argv[0];

  grub_dl_ref (my_mod);

  /* Initialize some global variables.  */
  address = 0;
  image_handle = 0;
  file_path = 0;
  dev_handle = 0;

  b = grub_efi_system_table->boot_services;

  if (argc > 1)
    {
      int i;
      grub_efi_char16_t *p16;

      for (i = 1, cmdline_len = 0; i < argc; i++)
        cmdline_len += grub_strlen (argv[i]) + 1;

      cmdline_len *= sizeof (grub_efi_char16_t);
      cmdline = p16 = grub_malloc (cmdline_len);
      if (! cmdline)
        goto fail;

      for (i = 1; i < argc; i++)
        {
          char *p8;

          p8 = argv[i];
          while (*p8)
            *(p16++) = *(p8++);

          *(p16++) = ' ';
        }
      *(--p16) = 0;
    }

  file = grub_file_open (filename, GRUB_FILE_TYPE_EFI_CHAINLOADED_IMAGE);
  if (! file)
    goto fail;

  /* Get the root device's device path.  */
  dev = grub_device_open (0);
  if (! dev)
    goto fail;

  if (dev->disk)
    dev_handle = grub_efidisk_get_device_handle (dev->disk);
  else if (dev->net && dev->net->server)
    {
      grub_net_network_level_address_t addr;
      struct grub_net_network_level_interface *inf;
      grub_net_network_level_address_t gateway;
      grub_err_t err;

      err = grub_net_resolve_address (dev->net->server, &addr);
      if (err)
	goto fail;

      err = grub_net_route_address (addr, &gateway, &inf);
      if (err)
	goto fail;

      dev_handle = grub_efinet_get_device_handle (inf->card);
    }

  if (dev_handle)
    dp = grub_efi_get_device_path (dev_handle);

  if (! dp)
    {
      grub_error (GRUB_ERR_BAD_DEVICE, "not a valid root device");
      goto fail;
    }

  file_path = make_file_path (dp, filename);
  if (! file_path)
    goto fail;

  grub_printf ("file path: ");
  grub_efi_print_device_path (file_path);

  fsize = grub_file_size (file);
  if (!fsize)
    {
      grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
		  filename);
      goto fail;
    }
  pages = (((grub_efi_uintn_t) fsize + ((1 << 12) - 1)) >> 12);

  status = efi_call_4 (b->allocate_pages, GRUB_EFI_ALLOCATE_ANY_PAGES,
			      GRUB_EFI_LOADER_CODE,
			      pages, &address);
  if (status != GRUB_EFI_SUCCESS)
    {
      grub_dprintf ("chain", "Failed to allocate %u pages\n",
		    (unsigned int) pages);
      grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
      goto fail;
    }

  boot_image = (void *) ((grub_addr_t) address);
  if (grub_file_read (file, boot_image, fsize) != fsize)
    {
      if (grub_errno == GRUB_ERR_NONE)
	grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
		    filename);

      goto fail;
    }

#if defined (__i386__) || defined (__x86_64__)
  if (fsize >= (grub_ssize_t) sizeof (struct grub_macho_fat_header))
    {
      struct grub_macho_fat_header *head = boot_image;
      if (head->magic
	  == grub_cpu_to_le32_compile_time (GRUB_MACHO_FAT_EFI_MAGIC))
	{
	  grub_uint32_t i;
	  struct grub_macho_fat_arch *archs
	    = (struct grub_macho_fat_arch *) (head + 1);
	  for (i = 0; i < grub_cpu_to_le32 (head->nfat_arch); i++)
	    {
	      if (GRUB_MACHO_CPUTYPE_IS_HOST_CURRENT (archs[i].cputype))
		break;
	    }
	  if (i == grub_cpu_to_le32 (head->nfat_arch))
	    {
	      grub_error (GRUB_ERR_BAD_OS, "no compatible arch found");
	      goto fail;
	    }
	  if (grub_cpu_to_le32 (archs[i].offset)
	      > ~grub_cpu_to_le32 (archs[i].size)
	      || grub_cpu_to_le32 (archs[i].offset)
	      + grub_cpu_to_le32 (archs[i].size)
	      > (grub_size_t) fsize)
	    {
	      grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
			  filename);
	      goto fail;
	    }
	  boot_image = (char *) boot_image + grub_cpu_to_le32 (archs[i].offset);
	  fsize = grub_cpu_to_le32 (archs[i].size);
	}
    }
#endif

#ifdef SUPPORT_SECURE_BOOT
  /* FIXME is secure boot possible also with universal binaries? */
  if (debug_secureboot || (grub_efi_get_secureboot () == GRUB_EFI_SECUREBOOT_MODE_ENABLED && grub_secure_validate ((void *)address, fsize)))
    {
      grub_file_close (file);
      grub_loader_set (grub_secureboot_chainloader_boot, grub_secureboot_chainloader_unload, 0);
      return 0;
    }
#endif

  status = efi_call_6 (b->load_image, 0, grub_efi_image_handle, file_path,
		       boot_image, fsize,
		       &image_handle);
  if (status != GRUB_EFI_SUCCESS)
    {
      if (status == GRUB_EFI_OUT_OF_RESOURCES)
	grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of resources");
      else
	grub_error (GRUB_ERR_BAD_OS, "cannot load image");

      goto fail;
    }

  /* LoadImage does not set a device handler when the image is
     loaded from memory, so it is necessary to set it explicitly here.
     This is a mess.  */
  loaded_image = grub_efi_get_loaded_image (image_handle);
  if (! loaded_image)
    {
      grub_error (GRUB_ERR_BAD_OS, "no loaded image available");
      goto fail;
    }
  loaded_image->device_handle = dev_handle;

  if (cmdline)
    {
      loaded_image->load_options = cmdline;
      loaded_image->load_options_size = cmdline_len;
    }

  grub_file_close (file);
  grub_device_close (dev);

  grub_loader_set (grub_chainloader_boot, grub_chainloader_unload, 0);
  return 0;

 fail:

  if (dev)
    grub_device_close (dev);

  if (file)
    grub_file_close (file);

  grub_free (file_path);

  if (address)
    efi_call_2 (b->free_pages, address, pages);

  if (cmdline)
    grub_free (cmdline);

  grub_dl_unref (my_mod);

  return grub_errno;
}

static grub_command_t cmd;

GRUB_MOD_INIT(chainloader)
{
  cmd = grub_register_command ("chainloader", grub_cmd_chainloader,
			       0, N_("Load another boot loader."));
  my_mod = mod;
}

GRUB_MOD_FINI(chainloader)
{
  grub_unregister_command (cmd);
}
