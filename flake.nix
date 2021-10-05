{
  description = "Security module for php7 and php8";

  # Nixpkgs / NixOS version to use.
  #inputs.nixpkgs.url = "nixpkgs/nixos-21.05";
  inputs.nixpkgs.url = "nixpkgs";

  outputs = { self, nixpkgs }:
    let

      # Generate a user-friendly version numer.
      version = builtins.substring 0 8 self.lastModifiedDate;

      # System types to support.
      supportedSystems = [ "x86_64-linux" "x86_64-darwin" ];

      # Helper function to generate an attrset '{ x86_64-linux = f "x86_64-linux"; ... }'.
      forAllSystems = f: nixpkgs.lib.genAttrs supportedSystems (system: f system);

      # Nixpkgs instantiated for supported system types.
      nixpkgsFor = forAllSystems (system: import nixpkgs { inherit system; overlays = [ self.overlay ]; });

    in

    {

      # A Nixpkgs overlay.
      overlay = final: prev: {

        snuffleupagus = with final; php.buildPecl rec {
          pname = "snuffleupagus";

          inherit version;

          buildInputs = [ pcre2 ];
          internalDeps = [ php.extensions.session ];

          doCheck = false;
          checkPhase = "make tests";

          src = ./src;

          configureFlags = [
            "--enable-snuffleupagus"
          ];

          postPhpize = ''
            ./configure --enable-snuffleupagus
          '';
        };

      };

      # Provide some binary packages for selected system types.
      packages = forAllSystems (system:
        let
          pkgs = nixpkgsFor.${system};
          snuffleupagus = pkgs.snuffleupagus;
        in
        {
          inherit snuffleupagus;
          php = pkgs.php.withExtensions ({ enabled, ... }: enabled ++ [ snuffleupagus ]);
        });

      # The default package for 'nix build'. This makes sense if the
      # flake provides only one package or there is a clear "main"
      # package.
      defaultPackage = forAllSystems (system: self.packages.${system}.snuffleupagus);

      # Tests run by 'nix flake check' and by Hydra.
      checks = forAllSystems
        (system:
          with nixpkgsFor.${system};

          {
            inherit (self.packages.${system}) snuffleupagus;

            # Additional tests, if applicable.
            test =
              let
                php = php.withExtensions ({ enabled, all }: enabled ++ [ snuffleupagus ]);
              in
              stdenv.mkDerivation {
                name = "snuffleupagus-test-${version}";

                buildInputs = [ snuffleupagus ];

                unpackPhase = "true";

                buildPhase = ''
                  echo 'running some integration tests'
                  [[ $(snuffleupagus) = 'Hello Nixers!' ]]
                '';

                installPhase = "mkdir -p $out";
              };
          }

          // lib.optionalAttrs stdenv.isLinux {
            # A VM test of the NixOS module.
            vmTest =
              with import (nixpkgs + "/nixos/lib/testing-python.nix")
                {
                  inherit system;
                };

              makeTest {
                nodes = {
                  client = { ... }: {
                    imports = [ self.nixosModules.snuffleupagus ];
                  };
                };

                testScript =
                  ''
                    start_all()
                    client.wait_for_unit("multi-user.target")
                    client.succeed("snuffleupagus")
                  '';
              };
          }
        );

    };
}
