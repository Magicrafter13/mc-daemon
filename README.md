# MC Daemon
Originally a daemon for running multiple Minecraft servers on a single host,
with a simple configuration and easy CLI interface, now a general purpose
supplement to systemd for those who hate systemd unit files.

# Using MC Daemon
## Getting the Program
1. Clone the repo.
2. Compile with `make`.
3. Optionally install with `make install` (run as root).

## Configuring the Daemon
Assuming you used `make install`, a sample config file has been placed for you
at `/etc/mc-daemon.conf`. This file is well commented, so will not be further
explained here. It follows INI style syntax - note that the name you put in
`[brackets]` is what you will have to type in the command line, so spaces and
other special characters are not recommended. If you installed, then you can
also get going right away with `systemctl enable --now mc-daemon`, and mc-daemon
will now auto start on boot.

## Using the Daemon Without Root
Copy the provided `mc-daemon-user.service` file to
`$XDG_CONFIG_HOME/systemd/user` (normally `~/.config/systemd/user`). Replace
paths of `/home/user` with the proper path to your home directory. If you
prefer, you can also change the paths used, though they follow default XDG base
directory specifications. Also make sure to update the MCD_CONFIG environment
variable to a real path (recommended to be `/home/user/.config/mc-daemon.conf`).

If you don't want to use systemd at all to run the daemon, all you have to do is
run the compiled executable (`mcd` after running `make`) with the following
environment variables: `MCD_CONFIG`, and `MCD_DATA`. The config variable is the
full path to `mc-daemon.conf` (see the included example), and the data variable
is a path to a directory where the PID file and socket file will be stored.
`$XDG_STATE_HOME` would make sense for the data directory, but so would
something under `/run/user`.
