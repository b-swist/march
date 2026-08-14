{
  description = "March WM Development Flake";

  inputs = {
    nixpkgs.url = "nixpkgs/nixos-26.05";
  };

  outputs = {nixpkgs, ...}: let
    systems = ["x86_64-linux" "arrch64-linux"];
    forAllSystems = nixpkgs.lib.genAttrs systems;
  in {
    devShells = forAllSystems (
      system: let
        pkgs = nixpkgs.legacyPackages.${system};
      in {
        default = pkgs.mkShell {
          packages = with pkgs; [
            pkg-config
            meson
            ninja
            wayland
            wayland-scanner
            libxkbcommon
            river
          ];
        };
      }
    );
  };
}
