{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
    devshell.url = "github:numtide/devshell";
  };

  outputs =
    inputs@{
      self,
      flake-parts,
      devshell,
      ...
    }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      imports = [
        devshell.flakeModule
      ];
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "i686-linux"
        "aarch64-darwin"
        "x86_64-darwin"
      ];
      perSystem =
        {
          pkgs,
          lib,
          system,
          ...
        }:
        let
          isDarwin = (lib.systems.elaborate system).isDarwin;
          nativeBuildInputs = lib.optional (!isDarwin) pkgs.glibc.static;
        in
        {
          devShells.default = pkgs.mkShellNoCC {
            inherit nativeBuildInputs;
            packages = [ pkgs.clang ];
            commands = [ ];
          };
          packages.default =
            let
              pkg =
                { stdenv }:
                stdenv.mkDerivation {
                  pname = "check-truecolor";
                  version =
                    if self ? shortRev then
                      "${self.lastModifiedDate}-${self.shortRev}"
                    else if self ? dirtyShortRev then
                      "${self.lastModifiedDate}-${self.dirtyShortRev}"
                    else
                      throw "Failed to build version string.";
                  src = ./.;

                  inherit nativeBuildInputs;

                  installPhase = ''
                    runHook preInstall

                    mkdir -p "$out/bin"
                    install -Dm755 ./check-truecolor "$out/bin/"

                    runHook postInstall
                  '';

                  meta = with lib; {
                    description = "Terminal's truecolor support detector using some terminal escape sequences.";
                    license = licenses.mit;
                    homepage = "https://github.com/mityu/cpp-check-truecolor";
                    platforms = platforms.unix;
                    mainProgram = "check-truecolor";
                  };
                };
            in
            pkgs.callPackage pkg { };
        };
    };
}
