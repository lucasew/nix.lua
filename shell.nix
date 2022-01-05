{pkgs ? import <nixpkgs> {}}:
pkgs.mkShell {
  buildInputs = with pkgs; [
    lua
    clang
    nix.dev
    ccls
  ];
}
