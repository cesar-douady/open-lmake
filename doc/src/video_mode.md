<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2026 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Video mode

If lmake is connected to a terminal, then the terminal foreground and background colors are probed and if the brightness of the background color is greater than that of the foreground color,
video mode is set to normal (dark text on light background), else it is set to reverse (light text on dark background).

In that case, lmake output is colored and the (configurable) color set is chosen depending on video mode.
