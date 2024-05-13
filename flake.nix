{
  description = "Tool to statically recompile N64 games into native executables";
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";

    flake-utils.url = "github:numtide/flake-utils";

    flake-compat = {
      url = "github:edolstra/flake-compat";
      flake = false;
    };
  };

  outputs = inputs @ {...}:
    inputs.flake-utils.lib.eachDefaultSystem
    (
      system: let
        pkgs = import inputs.nixpkgs {
          inherit system;
        };
      in rec {
        packages = rec {
          N64Recomp = pkgs.stdenv.mkDerivation {
            pname = "N64Recomp";
            version = "1.0.0";
            src = pkgs.fetchFromGitHub {
                  owner = "Mr-Wiseguy";
                  repo = "N64Recomp";
                  rev = "d7b223fde51c651d2e1aa856efaf19793a77e5e7";
                  deepClone = true;
                  fetchSubmodules = true;
                  hash = "sha256-TWr6ctsT3VaSRvsTfVBC5sBPCx4b2rvUuuZFUGO6kH4=";
                };
            nativeBuildInputs = [pkgs.cmake];

            installPhase = ''
                    mkdir -p $out/bin
                    cp N64Recomp $out/bin
                    cp RSPRecomp $out/bin
                '';
          };
          default = N64Recomp;
        };
        apps = rec {
          N64Recomp = {
            type = "app";
            program = "${packages.N64Recomp}/bin/N64Recomp";
          };

          RSPRecomp = {
            type = "app";
            program = "${packages.N64Recomp}/bin/RSPRecomp";
          };
          default = N64Recomp;
        };
      }
    );
}