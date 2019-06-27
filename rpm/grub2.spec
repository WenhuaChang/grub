#
# spec file for package grub2
#
# Copyright (c) 2019 SUSE LINUX GmbH, Nuernberg, Germany.
#
# All modifications and additions to the file contributed by third parties
# remain the property of their copyright owners, unless otherwise agreed
# upon. The license for this file, and modifications and additions to the
# file, is the same license as for the pristine package itself (unless the
# license for the pristine package is not an Open Source License, in which
# case the license is the MIT License). An "Open Source License" is a
# license that conforms to the Open Source Definition (Version 1.9)
# published by the Open Source Initiative.

# Please submit bugfixes or comments via http://bugs.opensuse.org/
#

%define _binaries_in_noarch_package_terminate_build 0

Name:           grub2
%ifarch x86_64 ppc64
BuildRequires:  gcc-32bit
BuildRequires:  glibc-32bit
BuildRequires:  glibc-devel-32bit
%else
BuildRequires:  gcc
BuildRequires:  glibc-devel
%endif
BuildRequires:  automake
BuildRequires:  bison
BuildRequires:  device-mapper-devel
BuildRequires:  fdupes
BuildRequires:  flex
BuildRequires:  freetype2-devel
BuildRequires:  fuse-devel
%if 0%{?suse_version} >= 1140
BuildRequires:  dejavu-fonts
BuildRequires:  gnu-unifont
%endif
BuildRequires:  help2man
BuildRequires:  xz
%if 0%{?suse_version} >= 1210
BuildRequires:  makeinfo
%else
BuildRequires:  texinfo
%endif
%if %{defined pythons}
BuildRequires:  %{pythons}
%else
BuildRequires:  python
%endif
BuildRequires:  xz-devel
%if 0%{?suse_version} > 1320
BuildRequires:  update-bootloader-rpm-macros
%endif

# Modules code is dynamically loaded and collected from a _fixed_ path.
%define _libdir %{_exec_prefix}/lib

# Build grub2-emu everywhere (it may be "required" by 'grub2-once')
%define emu 1

%ifarch ppc ppc64 ppc64le
%define grubcpu powerpc
%define platform ieee1275
# emu does not build here yet... :-(
%define emu 0
%endif

%ifarch %{ix86} x86_64
%define grubcpu i386
%define platform pc
%endif

%ifarch %{arm}
%define grubcpu arm
%define platform uboot
%endif

%ifarch aarch64
%define grubcpu arm64
%define platform efi
%define only_efi 1
%endif

%define grubarch %{grubcpu}-%{platform}

# build efi bootloader on some platforms only:
%if ! 0%{?efi:1}
%global efi %{ix86} x86_64 ia64 aarch64 %{arm}
%endif

%ifarch %{efi}
%ifarch %{ix86}
%define grubefiarch i386-efi
%else
%ifarch aarch64
%define grubefiarch arm64-efi
%else
%ifarch %{arm}
%define grubefiarch arm-efi
%else
%define grubefiarch %{_target_cpu}-efi
%endif
%endif
%endif
%endif

%ifarch %{ix86}
%define grubxenarch i386-xen
%endif

%ifarch x86_64
%define grubxenarch x86_64-xen
%endif

%if %{platform} == emu
# force %%{emu} to 1, e.g. for s390
%define emu 1
%endif

%if 0%{?suse_version} == 1110
%define only_efi %{nil}
%define only_x86_64 %{nil}
%endif

Version:        2.04~rc1
Release:        0
Summary:        Bootloader with support for Linux, Multiboot and more
License:        GPL-3.0-or-later
Group:          System/Boot
Url:            http://www.gnu.org/software/grub/
Source0:        grub-%{version}.tar.xz
Source1:        90_persistent
Source4:        grub2.rpmlintrc
# rsync -Lrtvz  translationproject.org::tp/latest/grub/ po
Source5:        translations-20170427.tar.xz

Requires:       gettext-runtime
%if 0%{?suse_version} >= 1140
Recommends:     os-prober
# xorriso not available using grub2-mkrescue (bnc#812681)
# downgrade to suggest as minimal system can't afford pulling in tcl/tk and half of the x11 stack (bsc#1102515)
Suggests:       libburnia-tools
Recommends:     mtools
%endif
Requires(post): /sbin/install-info
Requires(preun):/sbin/install-info
%if ! 0%{?only_efi:1}
Requires:       grub2-%{grubarch} = %{version}-%{release}
%endif
%ifarch ppc64 ppc64le
Requires:       powerpc-utils
%endif

BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%if 0%{?only_x86_64:1}
ExclusiveArch:  x86_64
%else
ExclusiveArch:  %{ix86} x86_64 ppc ppc64 ppc64le aarch64 %{arm}
%endif

%description
This is the second version of the GRUB (Grand Unified Bootloader), a
highly configurable and customizable bootloader with modular
architecture.  It support rich scale of kernel formats, file systems,
computer architectures and hardware devices.

This package includes user space utlities to manage GRUB on your system.


Authors:
--------
    Gordon Matzigkeit
    Yoshinori K. Okuji
    Colin Watson
    Colin D. Bennett
    Vesa Jääskeläinen
    Robert Millan
    Carles Pina

%package branding-upstream

Summary:        Upstream branding for GRUB2's graphical console
Group:          System/Fhs
Requires:       %{name} = %{version}

%description branding-upstream
Upstream branding for GRUB2's graphical console

%if ! 0%{?only_efi:1}
%package %{grubarch}

Summary:        Bootloader with support for Linux, Multiboot and more
Group:          System/Boot
%if %{platform} != emu
BuildArch:      noarch
%endif
Requires:       %{name} = %{version}
Requires(post):	%{name} = %{version}
%if 0%{?update_bootloader_requires:1}
%update_bootloader_requires
%else
Requires:       perl-Bootloader
Requires(post): perl-Bootloader
%endif

%description %{grubarch}
The GRand Unified Bootloader (GRUB) is a highly configurable and customizable
bootloader with modular architecture.  It supports rich variety of kernel formats,
file systems, computer architectures and hardware devices.  This subpackage
provides support for %{platform} systems.

%endif

%ifarch %{efi}

%package %{grubefiarch}

Summary:        Bootloader with support for Linux, Multiboot and more
# Require efibootmgr
# Without it grub-install is broken so break the package as well if unavailable
Group:          System/Boot
BuildArch:      noarch
Requires:       efibootmgr
Requires(post): efibootmgr
Requires:       %{name} = %{version}
Requires(post):	%{name} = %{version}
%if 0%{?update_bootloader_requires:1}
%update_bootloader_requires
%else
Requires:       perl-Bootloader >= 0.706
Requires(post): perl-Bootloader >= 0.706
%endif
Provides:       %{name}-efi = %{version}-%{release}
Obsoletes:      %{name}-efi < %{version}-%{release}

%description %{grubefiarch}
The GRand Unified Bootloader (GRUB) is a highly configurable and customizable
bootloader with modular architecture.  It supports rich variety of kernel formats,
file systems, computer architectures and hardware devices.  This subpackage
provides support for EFI systems.

%endif

%ifarch %{ix86} x86_64

%package %{grubxenarch}

Summary:        Bootloader with support for Linux, Multiboot and more
Group:          System/Boot
Provides:       %{name}-xen = %{version}-%{release}
Obsoletes:      %{name}-xen < %{version}-%{release}
BuildArch:      noarch

%description %{grubxenarch}
The GRand Unified Bootloader (GRUB) is a highly configurable and customizable
bootloader with modular architecture.  It supports rich variety of kernel formats,
file systems, computer architectures and hardware devices.  This subpackage
provides support for XEN systems.

%endif

%prep
# We create (if we build for efi) two copies of the sources in the Builddir
%setup -q -n grub-%{version} -a 5

%build
# collect evidence to debug spurious build failure on SLE15
ulimit -a

# Generate po/LINGUAS for message catalogs ...
./linguas.sh
# ... and make sure new catalogs are actually created
rm -f po/stamp-po

mkdir build
%ifarch %{efi}
mkdir build-efi
%endif
%ifarch %{ix86} x86_64
mkdir build-xen
%endif
%if %{emu}
mkdir build-emu
%endif

export PYTHON=%{_bindir}/python3
[ -x $PYTHON ] || unset PYTHON   # try 'python', if 'python3' is unavailable
# run bootstrap to import gnulib source to grub
./bootstrap --no-git --gnulib-srcdir=./gnulib
# autogen calls autoreconf -vi
./autogen.sh
# Not yet:
%define common_conf_options TARGET_LDFLAGS=-static --program-transform-name=s,grub,%{name},
# This does NOT work on SLE11:
%define _configure ../configure

# We don't want to let rpm override *FLAGS with default a.k.a bogus values.
CFLAGS="-fno-strict-aliasing -fno-inline-functions-called-once "
CXXFLAGS=" "
FFLAGS=" "
export CFLAGS CXXFLAGS FFLAGS

%if %{emu}
cd build-emu
%define arch_specific --enable-device-mapper --disable-grub-mount
TLFLAGS="-fPIC"

# -static is needed so that autoconf script is able to link
# test that looks for _start symbol on 64 bit platforms
../configure TARGET_LDFLAGS=$TLFLAGS	\
	--prefix=%{_prefix}		\
	--libdir=%{_datadir}		\
	--sysconfdir=%{_sysconfdir}	\
        --target=%{_target_platform}    \
        --with-platform=emu     \
	%{arch_specific}                \
        --program-transform-name=s,grub,%{name},
make %{?_smp_mflags}
cd ..
if [ "%{platform}" = "emu" ]; then
  rmdir build
  mv build-emu build
fi
%endif

%ifarch %{ix86} x86_64
cd build-xen
../configure                           \
        TARGET_LDFLAGS=-static         \
        --prefix=%{_prefix}            \
        --libdir=%{_datadir}           \
        --sysconfdir=%{_sysconfdir}    \
        --target=%{_target_platform}   \
        --with-platform=xen            \
        --program-transform-name=s,grub,%{name},
make %{?_smp_mflags}

cd ..
%endif

%ifarch %{efi}
cd build-efi
../configure   				                \
        TARGET_LDFLAGS=-static                          \
	--prefix=%{_prefix}				\
	--libdir=%{_datadir}				\
	--sysconfdir=%{_sysconfdir}			\
        --target=%{_target_platform}                    \
        --with-platform=efi                             \
        --program-transform-name=s,grub,%{name},
make %{?_smp_mflags}

cd ..
%endif

%if ! 0%{?only_efi:1}
cd build

# 64-bit x86-64 machines use 32-bit boot loader
# (We cannot just redefine _target_cpu, as we'd get i386.rpm packages then)
%ifarch x86_64
%define _target_platform i386-%{_vendor}-%{_target_os}%{?_gnu}
%endif

%if %{platform} != "emu"
%define arch_specific --enable-device-mapper
TLFLAGS="-static"

# -static is needed so that autoconf script is able to link
# test that looks for _start symbol on 64 bit platforms
../configure TARGET_LDFLAGS="$TLFLAGS"	\
	--prefix=%{_prefix}		\
	--libdir=%{_datadir}		\
	--sysconfdir=%{_sysconfdir}	\
        --target=%{_target_platform}    \
        --with-platform=%{platform}     \
	%{arch_specific}                \
        --program-transform-name=s,grub,%{name},
make %{?_smp_mflags}
%endif
cd ..
%endif

%install

%ifarch %{ix86} x86_64
cd build-xen
%make_install
cd ..
%endif

%ifarch %{efi}
cd build-efi
%make_install
cd ..
%endif

%if ! 0%{?only_efi:1}
cd build
%make_install
cd ..
%endif

if [ "%{platform}" = "emu" ]; then
  # emu-lite is currently broken (and not needed), don't install!
  rm -f %{buildroot}/%{_bindir}/%{name}-emu-lite
elif [ -d build-emu/grub-core ]; then
  cd build-emu/grub-core
  install -m 755 grub-emu %{buildroot}/%{_bindir}/%{name}-emu
  if false; then
    # this needs to go to '-emu'-package; until that is ready, don't install!
    install -m 755 grub-emu-lite %{buildroot}/%{_bindir}/%{name}-emu-lite
  else
    rm -f %{buildroot}/%{_bindir}/%{name}-emu-lite
  fi
  install -m 644 grub-emu.1 %{buildroot}/%{_mandir}/man1/%{name}-emu.1
  cd ../..
fi

# *.module files are installed with executable bits due to the way grub2 build
# system works. Clear executable bits to not confuse find-debuginfo.sh
find %{buildroot}/%{_datadir}/%{name} \
       \( -name '*.module' -o -name '*.image' -o -name '*.exec' \) -print0 | \
       xargs --no-run-if-empty -0 chmod a-x

# Script that makes part of grub.cfg persist across updates
install -m 755 %{SOURCE1} %{buildroot}/%{_sysconfdir}/grub.d/

# Ghost config file
install -d %{buildroot}/boot/%{name}
touch %{buildroot}/boot/%{name}/grub.cfg

# Remove devel files
rm %{buildroot}/%{_datadir}/%{name}/*/*.h
%if 0%{?suse_version} >= 1140
rm %{buildroot}/%{_datadir}/%{name}/*.h
%endif

%find_lang %{name}
%fdupes %buildroot%{_bindir}
%fdupes %buildroot%{_libdir}
%fdupes %buildroot%{_datadir}

%pre

%post
/sbin/install-info %{_infodir}/grub-dev.info %{_infodir}/dir || :
/sbin/install-info %{_infodir}/%{name}.info %{_infodir}/dir || :

%if ! 0%{?only_efi:1}

%post %{grubarch}
%if 0%{?update_bootloader_check_type_reinit_post:1}
%update_bootloader_check_type_reinit_post grub2
%else
# To check by current loader settings
if [ -f %{_sysconfdir}/sysconfig/bootloader ]; then
  . %{_sysconfdir}/sysconfig/bootloader
fi

# If the grub is the current loader, we'll handle the grub2 testing entry
if [ "x${LOADER_TYPE}" = "xgrub" ]; then

  exec >/dev/null 2>&1

  # check if entry for grub2's core.img exists in the config
  # if yes, we will correct obsoleted path and update grub2 stuff and config to make it work
  # if no, do nothing
  if [ -f /boot/grub/menu.lst ]; then

    # If grub config contains obsolete core.img path, remove and use the new one
    if /usr/bin/grep -l "^\s*kernel\s*.*/boot/%{name}/core.img" /boot/grub/menu.lst; then
      /sbin/update-bootloader --remove --image /boot/%{name}/core.img || true
      /sbin/update-bootloader --add --image /boot/%{name}/i386-pc/core.img --name "GNU GRUB 2" || true
    fi

    # Install grub2 stuff and config to make the grub2 testing entry to work with updated version
    if /usr/bin/grep -l "^\s*kernel\s*.*/boot/%{name}/i386-pc/core.img" /boot/grub/menu.lst; then
      # Determine the partition with /boot
      BOOT_PARTITION=$(df -h /boot | sed -n '2s/[[:blank:]].*//p')
      # Generate core.img, but don't let it be installed in boot sector
      %{name}-install --no-bootsector $BOOT_PARTITION || true
      # Create a working grub2 config, otherwise that entry is un-bootable
      /usr/sbin/grub2-mkconfig -o /boot/%{name}/grub.cfg
    fi
  fi

elif [ "x${LOADER_TYPE}" = "xgrub2" ]; then

  # It's enought to call update-bootloader to install grub2 and update it's config
  # Use new --reinit, if not available use --refresh
  # --reinit: install and update bootloader config
  # --refresh: update bootloader config
  /sbin/update-bootloader --reinit 2>&1 | grep -q 'Unknown option: reinit' &&
  /sbin/update-bootloader --refresh || true
fi
%endif

%posttrans %{grubarch}
%{?update_bootloader_posttrans}

%endif

%ifarch %{efi}

%post %{grubefiarch}
%if 0%{?update_bootloader_check_type_reinit_post:1}
%update_bootloader_check_type_reinit_post grub2-efi
%else
# To check by current loader settings
if [ -f %{_sysconfdir}/sysconfig/bootloader ]; then
  . %{_sysconfdir}/sysconfig/bootloader
fi

if [ "x${LOADER_TYPE}" = "xgrub2-efi" ]; then

  if [ -d /boot/%{name}-efi ]; then
    # Migrate settings to standard prefix /boot/grub2
    for i in custom.cfg grubenv; do
      [ -f /boot/%{name}-efi/$i ] && cp -a /boot/%{name}-efi/$i /boot/%{name} || :
    done

  fi

  # It's enough to call update-bootloader to install grub2 and update it's config
  # Use new --reinit, if not available use --refresh
  # --reinit: install and update bootloader config
  # --refresh: update bootloader config
  /sbin/update-bootloader --reinit 2>&1 | grep -q 'Unknown option: reinit' &&
  /sbin/update-bootloader --refresh || true
fi

if [ -d /boot/%{name}-efi ]; then
  mv /boot/%{name}-efi /boot/%{name}-efi.rpmsave
fi

exit 0
%endif

%posttrans %{grubefiarch}
%{?update_bootloader_posttrans}

%endif

%preun
if [ $1 = 0 ]; then
  /sbin/install-info --delete %{_infodir}/grub-dev.info %{_infodir}/dir || :
  /sbin/install-info --delete %{_infodir}/%{name}.info %{_infodir}/dir || :

# We did not add core.img to grub1 menu.lst in new update-bootloader macro as what
# the old %%post ever did, then the %%preun counterpart which removed the added core.img
# entry from old %%post can be skipped entirely if having new macro in use.
%if ! 0%{?update_bootloader_posttrans:1}%{?only_efi:1}
  # To check by current loader settings
  if [ -f %{_sysconfdir}/sysconfig/bootloader ]; then
    . %{_sysconfdir}/sysconfig/bootloader
  fi

  if [ "x${LOADER_TYPE}" = "xgrub" ]; then

    exec >/dev/null 2>&1

    if [ -f /boot/grub/menu.lst ]; then

      # Remove grub2 testing entry in menu.lst if has any
      for i in /boot/%{name}/core.img /boot/%{name}/i386-pc/core.img; do
        if /usr/bin/grep -l "^\s*kernel\s*.*$i" /boot/grub/menu.lst; then
          /sbin/update-bootloader --remove --image "$i" || true
        fi
      done
    fi

    # Cleanup config, to not confuse some tools determining bootloader in use
    rm -f /boot/%{name}/grub.cfg

    # Cleanup installed files
    # Unless grub2 provides grub2-uninstall, we don't remove any file because
    # we have no idea what's been installed. (And a blind remove is dangerous
    # to remove user's or other package's file accidently ..)
  fi
%endif
fi

%postun

%files -f %{name}.lang
%defattr(-,root,root,-)
%if 0%{?suse_version} < 1500
%doc COPYING
%else
%license COPYING
%endif
%doc NEWS README
%doc THANKS TODO
%doc docs/autoiso.cfg docs/osdetect.cfg
%dir /boot/%{name}
%ghost /boot/%{name}/grub.cfg
%{_sysconfdir}/bash_completion.d/grub
%dir %{_sysconfdir}/grub.d
%{_sysconfdir}/grub.d/README
%config(noreplace) %{_sysconfdir}/grub.d/00_header
%config(noreplace) %{_sysconfdir}/grub.d/10_linux
%config(noreplace) %{_sysconfdir}/grub.d/20_linux_xen
%config(noreplace) %{_sysconfdir}/grub.d/40_custom
%config(noreplace) %{_sysconfdir}/grub.d/41_custom
%config(noreplace) %{_sysconfdir}/grub.d/90_persistent
%{_sbindir}/%{name}-install
%{_sbindir}/%{name}-mkconfig
%{_sbindir}/%{name}-probe
%{_sbindir}/%{name}-reboot
%{_sbindir}/%{name}-set-default
%{_bindir}/%{name}-editenv
%{_bindir}/%{name}-file
%{_bindir}/%{name}-fstest
%{_bindir}/%{name}-kbdcomp
%{_bindir}/%{name}-menulst2cfg
%{_bindir}/%{name}-mkfont
%{_bindir}/%{name}-mkimage
%{_bindir}/%{name}-mklayout
%{_bindir}/%{name}-mknetdir
%{_bindir}/%{name}-mkpasswd-pbkdf2
%{_bindir}/%{name}-mkrelpath
%{_bindir}/%{name}-mkrescue
%{_bindir}/%{name}-mkstandalone
%{_bindir}/%{name}-render-label
%{_bindir}/%{name}-script-check
%{_bindir}/%{name}-syslinux2cfg
%dir %{_datadir}/%{name}
%dir %{_datadir}/%{name}/themes
%if 0%{?suse_version} >= 1140
%{_datadir}/%{name}/*.pf2
%endif
%{_datadir}/%{name}/grub-mkconfig_lib
%{_infodir}/grub-dev.info*
%{_infodir}/%{name}.info*
%{_mandir}/man1/%{name}-editenv.1.*
%{_mandir}/man1/%{name}-file.1.*
%{_mandir}/man1/%{name}-fstest.1.*
%{_mandir}/man1/%{name}-kbdcomp.1.*
%{_mandir}/man1/%{name}-menulst2cfg.1.*
%{_mandir}/man1/%{name}-mkfont.1.*
%{_mandir}/man1/%{name}-mkimage.1.*
%{_mandir}/man1/%{name}-mklayout.1.*
%{_mandir}/man1/%{name}-mknetdir.1.*
%{_mandir}/man1/%{name}-mkpasswd-pbkdf2.1.*
%{_mandir}/man1/%{name}-mkrelpath.1.*
%{_mandir}/man1/%{name}-mkrescue.1.*
%{_mandir}/man1/%{name}-mkstandalone.1.*
%{_mandir}/man1/%{name}-render-label.1.*
%{_mandir}/man1/%{name}-script-check.1.*
%{_mandir}/man1/%{name}-syslinux2cfg.1.*
%{_mandir}/man8/%{name}-install.8.*
%{_mandir}/man8/%{name}-mkconfig.8.*
%{_mandir}/man8/%{name}-probe.8.*
%{_mandir}/man8/%{name}-reboot.8.*
%{_mandir}/man8/%{name}-set-default.8.*
%if %{emu}
%{_bindir}/%{name}-emu
%{_mandir}/man1/%{name}-emu.1.*
%endif
%config(noreplace) %{_sysconfdir}/grub.d/30_os-prober
%{_bindir}/%{name}-glue-efi
%{_bindir}/%{name}-mount
%{_sbindir}/%{name}-bios-setup
%{_sbindir}/%{name}-macbless
%{_sbindir}/%{name}-ofpathname
%{_sbindir}/%{name}-sparc64-setup
%{_mandir}/man1/%{name}-glue-efi.1.*
%{_mandir}/man1/%{name}-mount.1.*
%{_mandir}/man8/%{name}-bios-setup.8.*
%{_mandir}/man8/%{name}-macbless.8.*
%{_mandir}/man8/%{name}-ofpathname.8.*
%{_mandir}/man8/%{name}-sparc64-setup.8.*

%files branding-upstream
%defattr(-,root,root,-)
%{_datadir}/%{name}/themes/starfield

%if ! 0%{?only_efi:1}

%files %{grubarch}
%defattr(-,root,root,-)
%dir %{_datadir}/%{name}/%{grubarch}
%ifarch ppc ppc64 ppc64le
# This is intentionally "grub.chrp" and not "%%{name}.chrp"
%{_datadir}/%{name}/%{grubarch}/grub.chrp
%{_datadir}/%{name}/%{grubarch}/bootinfo.txt
%endif
%ifnarch ppc ppc64 ppc64le %{arm}
%{_datadir}/%{name}/%{grubarch}/*.image
%endif
%{_datadir}/%{name}/%{grubarch}/*.img
%{_datadir}/%{name}/%{grubarch}/*.lst
%{_datadir}/%{name}/%{grubarch}/*.mod
%{_datadir}/%{name}/%{grubarch}/*.module
%ifarch x86_64
%{_datadir}/%{name}/%{grubarch}/efiemu*.o
%endif
%{_datadir}/%{name}/%{grubarch}/gdb_grub
%{_datadir}/%{name}/%{grubarch}/gmodule.pl
%{_datadir}/%{name}/%{grubarch}/kernel.exec
%{_datadir}/%{name}/%{grubarch}/modinfo.sh
%endif

%ifarch %{efi}

%files %{grubefiarch}
%defattr(-,root,root,-)
%dir %{_datadir}/%{name}/%{grubefiarch}
%{_datadir}/%{name}/%{grubefiarch}/*.img
%{_datadir}/%{name}/%{grubefiarch}/*.lst
%{_datadir}/%{name}/%{grubefiarch}/*.mod
%{_datadir}/%{name}/%{grubefiarch}/*.module
%{_datadir}/%{name}/%{grubefiarch}/gdb_grub
%{_datadir}/%{name}/%{grubefiarch}/gmodule.pl
%{_datadir}/%{name}/%{grubefiarch}/kernel.exec
%{_datadir}/%{name}/%{grubefiarch}/modinfo.sh
%if 0%{?suse_version} < 1600
%endif

%endif

%ifarch %{ix86} x86_64
%files %{grubxenarch}
%defattr(-,root,root,-)
%dir %{_datadir}/%{name}/%{grubxenarch}
%{_datadir}/%{name}/%{grubxenarch}/*
%endif

%changelog