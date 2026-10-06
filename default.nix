{ lib, stdenv }:

stdenv.mkDerivation {
  pname = "scrollkey";
  version = "0.1.0";

  src = lib.cleanSourceWith {
    src = ./.;
    filter =
      path: type:
      !(builtins.elem (builtins.baseNameOf path) [
        ".git"
        "build"
        ".DS_Store"
      ]);
  };

  # The Nix compiler supplies its own macOS SDK; do not call host xcrun.
  makeFlags = [
    "CC=cc"
    "SDKFLAGS="
    "PREFIX=$(out)"
  ];

  doCheck = true;
  checkPhase = ''
    runHook preCheck
    make CC=cc SDKFLAGS= build/scroll-test build/cli-test
    ./build/scroll-test
    sh tests/cli.sh ./build/cli-test
    runHook postCheck
  '';

  # Preserve the linker's ad-hoc signature for macOS Accessibility checks.
  dontStrip = true;

  meta = {
    description = "Hold a modifier and move a mouse or trackball to scroll";
    homepage = "https://github.com/jaybonthius/scrollkey";
    license = lib.licenses.mit;
    platforms = lib.platforms.darwin;
    mainProgram = "scrollkey";
  };
}
