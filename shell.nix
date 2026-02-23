{pkgs ? import <nixpkgs> {}}:
pkgs.mkShell {
  buildInputs = with pkgs; [
    lua
    clang
    clang-tools
    nix.dev
    ccls
  ];
}
