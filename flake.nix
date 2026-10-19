{
  description = "Six band equalizer with loudness equalization and bass boost for GNOME";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAllSystems (pkgs: {
        default = pkgs.callPackage ./packaging/nix/package.nix { };
      });

      nixosModules.default = import ./packaging/nix/module.nix self;

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell { inputsFrom = [ self.packages.${pkgs.stdenv.hostPlatform.system}.default ]; };
      });
    };
}
