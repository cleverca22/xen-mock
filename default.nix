with import <nixpkgs>{};

stdenv.mkDerivation {
  name = "xen-mock";
  buildInputs = [ xen libelf ];
  src = ./.;
}
