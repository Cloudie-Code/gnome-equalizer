self:
{
  config,
  lib,
  pkgs,
  ...
}:

let
  cfg = config.programs.gnome-equalizer;
in
{
  options.programs.gnome-equalizer = {
    enable = lib.mkEnableOption "GNOME Equalizer (audio filter, settings app and Shell extension)";
    package = lib.mkOption {
      type = lib.types.package;
      default = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
      description = "The gnome-equalizer package to use.";
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ];
    systemd.packages = [ cfg.package ];
    systemd.user.services.gnome-equalizer.wantedBy = [ "pipewire.service" ];
  };
}
