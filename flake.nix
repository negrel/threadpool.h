{
  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs =
    {
      nixpkgs,
      flake-utils,
      ...
    }:
    let
      outputsWithoutSystem = { };
      outputsWithSystem = flake-utils.lib.eachDefaultSystem (
        system:
        let
          pkgs = import nixpkgs {
            inherit system;
          };
        in
        {
          devShells = {
            default = pkgs.mkShell {
              buildInputs = with pkgs; [
                clang-tools
                valgrind
              ];
            };
          };
          packages = {
            default = derivation {
              name = "threadpool.h";
              version = "0.1.0";
              system = system;
              builder = "${pkgs.bash}/bin/bash";
              args = [
                "-c"
                "${pkgs.coreutils}/bin/mkdir -p $out/include; ${pkgs.coreutils}/bin/cp ${./threadpool.h} $out/include/threadpool.h"
              ];
            };
          };
        }
      );
    in
    outputsWithSystem // outputsWithoutSystem;
}
