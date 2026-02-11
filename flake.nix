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

        mount -t proc none      /proc
        mount -t sysfs none     /sys
        mount -t devtmpfs none  /dev

        mdev -s
      '';

      initrd = pkgs.runCommand "build-initrd" {
        buildInputs = [ pkgs.cpio pkgs.gzip pkgs.fakeroot ];
      } ''
        tempdir=$(mktemp -d)
        (cd $tempdir
          fakeroot mkdir dev proc sys etc/init.d -p
          fakeroot cp -rf ${pkgs.pkgsStatic.busybox}/* .
          fakeroot rm -f default.script

          # system-v init script
          fakeroot cp -rf ${init} etc/init.d/rcS

          find . -print0 | cpio --owner=root:root --null -ov --format=newc \
            | gzip -9 > $out
        )
      '';

      runvm = pkgs.writeShellScriptBin "runvm" ''
        ${pkgs.qemu}/bin/qemu-system-x86_64 -enable-kvm -nographic \
          -smp 4 -m 4G -kernel ${pkgs.linuxPackages_latest.kernel}/bzImage \
          -initrd ${initrd} -append "console=ttyS0 rdinit=/linuxrc"
      '';

      runvm-xen = pkgs.writeShellScriptBin "runvm" ''
        set -x
        tempdir=$(mktemp -d)
        (cd $tempdir
          cp ${pkgs.linuxPackages_latest.kernel}/bzImage  linux.bin
          cp ${initrd}                                    initrd.bin
          gunzip -c ${xen}/xen.gz                       > xen.bin

          xenargs="dom0_mem=512M console=com1 loglvl=error noreboot"
          dom0args="console=hvc0 rdinit=/linuxrc earlyprintk=xen loglevel=4"

          ${pkgs.qemu}/bin/qemu-system-x86_64 -enable-kvm -nographic \
            -smp 4 -m 4G -kernel xen.bin \
            -initrd "linux.bin,initrd.bin" \
            -append "$xenargs -- $dom0args"
        )
      '';
    in
    {

      packages.x86_64-linux.xen = xen;
      packages.x86_64-linux.init = init;
      packages.x86_64-linux.runvm = runvm;
      packages.x86_64-linux.runvm-xen = runvm-xen;

      packages.x86_64-linux.default = self.packages.x86_64-linux.runvm;

    };
}
