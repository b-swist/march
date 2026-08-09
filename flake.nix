{
  description = "March WM Development Flake";

  inputs = {
    nixpkgs.url = "nixpkgs/nixos-26.05";
  };

  outputs = {nixpkgs, ...}: let
    system = "x86_64-linux";
    pkgs = import nixpkgs {inherit system;};
  in {
    devShells.${system}.default = pkgs.mkShell {
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
  };
}
