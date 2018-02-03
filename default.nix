with import <nixpkgs>{};

let
  termcolor = callPackage ./termcolor.nix {};
in stdenv.mkDerivation {
  name = "xen-mock";
  buildInputs = [ xen libelf termcolor ];
  src = ./.;
  dontStrip = true;
}
