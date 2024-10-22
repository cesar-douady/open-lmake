# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# ut contains the unit test helper functions
# ut.lmake runs an lmake command and counts output lines per keyword
# e.g. done=2 means there are 2 lines with the done keyword
import ut

# Note the may_rerun status on some lines :
# This means that new deps have been discovered and they were out-of-date.
# So, this run missed the point.
# However, we may be lucky and it could be that when built, the deps do not change their content.
# In that case, the job will not be rerun (bu t this is not the case here when run from a virgin repository).
ut.lmake('hello_world.ok',new=11,done=14,may_rerun=2,steady=1)
