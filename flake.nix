# vim: ts=2 sw=2 ai et cc=80
{
  description = "A very basic flake";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-25.11-small";
  };

  outputs = { self, nixpkgs }:
    let
      pkgs = import nixpkgs { system = "x86_64-linux"; };

      xen = pkgs.stdenv.mkDerivation rec {
        pname = "xen-bin";
        version = "RELEASE-4.17.6";

        nativeBuildInputs = with pkgs; [
          git flex bison python3
        ];

        configureFlags = [
          "--disable-tools"
          "--disable-docs"
          "--disable-stubdom"
        ];

        src = pkgs.fetchFromGitHub {
          owner = "xen-project";
          repo = "xen";
          rev = "${version}";
          hash = "sha256-j0PpK2KMWl+brcs9+h3cbiy9HiK9m5v55ghm4awrv1c=";
        };

        installPhase = ''
          mkdir $out
          cp -rf dist/install/boot/* $out/
        '';
      };

      init = pkgs.writeScript "init" ''
        #!/bin/sh

        mount -t proc     none  /proc
        mount -t sysfs    none  /sys
        mount -t devtmpfs none  /dev

        for s in /etc/init.d/S*; do
          sh $s
        done
      '';

      s10-modules = pkgs.writeScript "S10modules" ''
        #!/bin/sh

        modprobe -q i8042 > /dev/null 2>&1 
        modprobe -q atkbd > /dev/null 2>&1 
        modprobe -q bochs > /dev/null 2>&1 
      '';

      s99-graphic = pkgs.writeScript "S99graphic" ''
        /bin/meter
      '';

      meter = pkgs.pkgsStatic.stdenv.mkDerivation {
        pname = "meter";
        version = "unstable";

        src = ./src;

        buildInputs = [ pkgs.pkgsStatic.libdrm ];

        phases = [ "unpackPhase" "installPhase" ];

        installPhase = ''
          mkdir $out/bin -p
          $CC -g -static -o $out/bin/meter meter.c \
            -I${pkgs.pkgsStatic.libdrm.dev}/include/libdrm \
            ${pkgs.pkgsStatic.libdrm}/lib/libdrm.a
        '';
      };

      conf = pkgs.writeText "lv_conf.h" ''
        #ifndef LV_CONF_H
        #define LV_CONF_H

        #define LV_USE_LINUX_DRM            1
        #define LV_BUILD_DEMOS              1
        #define LV_USE_DEMO_WIDGETS         1

        #define LV_USE_LOG                  1
        #endif // LV_CONF_H
      '';

      lvgl = pkgs.pkgsStatic.stdenv.mkDerivation rec {
        pname = "lvgl";
        version = "v9.4.0";

        src = pkgs.fetchFromGitHub {
          owner = "lvgl";
          repo = "lvgl";
          rev = "${version}";
          hash = "sha256-DSk+c2T3D9qgYhyPNPjGHpipdwFwOkgMJ7vwrjDyjyE=";
        };

        buildInputs = [ pkgs.pkgsStatic.libdrm.dev ];
        nativeBuildInputs = [ pkgs.cmake pkgs.pkg-config ];

        postPatch = ''
          cp ${conf} lv_conf.h
          cp ${pkgs.pkgsStatic.libdrm.dev}/include/libdrm/*.h .
        '';
      };

      meter-lvgl = pkgs.pkgsStatic.stdenv.mkDerivation {
        pname = "meter-lvgl";
        version = "unstable";

        src = ./src;

        buildInputs = [ pkgs.pkgsStatic.libdrm ];

        phases = [ "unpackPhase" "installPhase" ];

        installPhase = ''
          mkdir $out/bin -p
          cp ${conf} lv_conf.h

          $CC -g -static -o $out/bin/meter-lvgl meter-lvgl.c \
            -I. -DLV_CONF_INCLUDE_SIMPLE=1 \
            -I${pkgs.pkgsStatic.libdrm.dev}/include/libdrm \
            -I${lvgl}/include \
              ${lvgl}/lib/liblvgl_demos.a \
              ${lvgl}/lib/liblvgl_examples.a \
              ${lvgl}/lib/liblvgl.a \
              ${pkgs.pkgsStatic.libdrm}/lib/libdrm.a
        '';
      };


      # kernel = pkgs.linuxPackages_latest.kernel;
      kernel = pkgs.linuxPackages_rt_6_1.kernel;

      initrd = pkgs.runCommand "build-initrd" {
        buildInputs = [ pkgs.cpio pkgs.gzip pkgs.fakeroot ];
      } ''
        tempdir=$(mktemp -d)
        (cd $tempdir
          fakeroot mkdir dev proc sys etc/init.d -p
          fakeroot cp -rf ${pkgs.pkgsStatic.busybox}/* .
          fakeroot rm -f default.script

          fakeroot cp -rf ${kernel.modules}/*  .

          # system-v init script
          fakeroot cp -rf ${init} etc/init.d/rcS
          fakeroot cp -rf ${s10-modules} etc/init.d/S10modules
          # fakeroot cp -rf ${s99-graphic} etc/init.d/S99graphic

          fakeroot cp -rf ${meter}/* .
          fakeroot cp -rf ${meter-lvgl}/* .

          find . -print0 | cpio --owner=root:root --null -ov --format=newc \
            | gzip -9 > $out
        )
      '';

      runvm = pkgs.writeShellScriptBin "runvm" ''
        ${pkgs.qemu}/bin/qemu-system-x86_64 -enable-kvm -nographic \
          -smp 4 -m 4G -kernel ${kernel}/bzImage \
          -initrd ${initrd} -append "console=ttyS0 rdinit=/linuxrc"
      '';

      runvm-with-xen = pkgs.writeShellScriptBin "runvm" ''
        tempdir=$(mktemp -d)
        (cd $tempdir
          cp -rf ${kernel}/bzImage                              linux.bin
          cp -rf ${initrd}                                      initrd.bin
          gunzip -c ${xen}/xen.gz                             > xen.bin

          xenargs="dom0_mem=512M console=com1 loglvl=error noreboot"
          dom0args="root=/dev/ram0 console=tty0 rdinit=/linuxrc earlyprintk=xen loglevel=4"

          ${pkgs.qemu}/bin/qemu-system-x86_64 -enable-kvm \
            -smp 4 -m 4G -device virtio-gpu \
            -kernel xen.bin \
            -initrd "linux.bin,initrd.bin" \
            -append "$xenargs -- $dom0args"
        )
      '';
    in
    {

      packages.x86_64-linux.xen = xen;
      packages.x86_64-linux.init = init;
      packages.x86_64-linux.runvm = runvm;
      packages.x86_64-linux.lvgl = lvgl;
      packages.x86_64-linux.meter-lvgl = meter-lvgl;
      packages.x86_64-linux.runvm-with-xen = runvm-with-xen;

      packages.x86_64-linux.default = self.packages.x86_64-linux.runvm-with-xen;

    };
}
