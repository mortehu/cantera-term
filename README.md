# About

cantera-term is a terminal emulator for X11

# Copyright

Morten Hustveit 2006, 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016.

# Usage

Use Shift in combination with Up, Down, PageUp, PageDown, Home and End to
scroll.

Use the middle mouse button or Shift+Insert to paste the primary selection.

Use Ctrl+Shift+Insert to paste the clipboard.

Press the menu key or the right Control key twice to replace the expression
to the left of the cursor with its computed value, as displayed at the right
edge of the terminal window.

# Configuration

The path of the configuration files is `$HOME/.cantera/config`.

Terminal configuration:

    terminal.history-size <history-size>
    terminal.font <font-path>
    terminal.font-size <font-size>
    terminal.palette <palette>

## Example Palettes

### ANSI colors:

    terminal.palette "000000 1818c2 18c218 18c2c2 c21818 c218c2 c2c218 c2c2c2
                      686868 7474ff 54ff54 54ffff ff5454 ff54ff ffff54 ffffff" 

### Linux Console (according to gnome-terminal):

    terminal.palette "000000 0000aa 00aa00 00aaaa aa0000 aa00aa aaaa00 aaaaaa
                      555555 5555ff 55ff55 55ffff ff5555 ff55ff ffff55 ffffff"

### Tango (modified to gray on black):

    terminal.palette "000000 3465a4 4e9a06 06989a cc0000 75507b c4a000 c2c2c2
                      555753 729fcf 8ae234 34e2e2 ef2929 ad7fa8 fce94f eeeeec"
